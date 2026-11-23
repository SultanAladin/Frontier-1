/*============================================================================================================================================
                                                           CAMERAINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance CameraInspector with own retained optical lens & F78 Mount (On tripod / Floating) diagrams

#include "Engine/Editor/Inspectors/CameraInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

[[nodiscard]] float SegmentDistance(float PX, float PY, float AX, float AY, float BX, float BY) noexcept
{
    const float VX = BX - AX, VY = BY - AY;
    const float Den = VX * VX + VY * VY;
    const float T = (Den > 1e-5f) ? std::clamp(((PX - AX) * VX + (PY - AY) * VY) / Den, 0.0f, 1.0f) : 0.0f;
    const float DX = PX - (AX + VX * T), DY = PY - (AY + VY * T);
    return std::sqrt(DX * DX + DY * DY);
}
} // namespace

void CameraInspector::UpdateLensSurface(const CameraInstanceData& Instance)
{
    uint64_t Sig = 0xCA010001ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.FocalLengthMm);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.ApertureFStop);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.LensFlare.Enabled ? 1ull : 0ull);

    const uint32_t W = 224u, H = 142u;
    LensPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f, CY = static_cast<float>(H) * 0.5f, OuterR = 48.0f;
        const float ApertureR = std::clamp(32.0f / std::max(1.2f, Instance.ApertureFStop), 6.0f, 34.0f);
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX, DY = static_cast<float>(Y) + 0.5f - CY;
                const float Dist = std::sqrt(DX * DX + DY * DY);
                float R = 0.045f, G = 0.048f, B = 0.058f;
                if (Dist < OuterR)
                {
                    const float Norm = Dist / OuterR;
                    if (Dist > OuterR - 6.0f) { R = 0.14f; G = 0.145f; B = 0.16f; }
                    else if (Dist > ApertureR)
                    {
                        const float Blade = 0.08f + 0.04f * std::sin(std::atan2(DY, DX) * 7.0f);
                        R = Blade; G = Blade * 1.03f; B = Blade * 1.12f;
                    }
                    else
                    {
                        const float Coat = std::exp(-std::pow((Norm - 0.35f) * 3.5f, 2.0f));
                        const float Glint = std::exp(-((DX + 14.0f) * (DX + 14.0f) + (DY + 14.0f) * (DY + 14.0f)) / 90.0f);
                        R = 0.05f + Coat * 0.12f + Glint * 0.72f + (Instance.LensFlare.Enabled ? Coat * 0.18f : 0.0f);
                        G = 0.09f + Coat * 0.24f + Glint * 0.85f;
                        B = 0.16f + Coat * 0.42f + Glint * 1.05f;
                    }
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

void CameraInspector::UpdateMountSurface(const CameraInstanceData& Instance)
{
    uint64_t Sig = 0xCA020001ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.TripodMounted ? 1ull : 0ull);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.MountHeightMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.CameraRollDegrees);

    const uint32_t W = 240u, H = 148u;
    MountPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const bool  Floating = !Instance.TripodMounted;
        const float GroundY  = 126.0f;
        const float CamX     = 118.0f;
        const float Scale    = Floating ? 12.0f : 38.0f;
        const float CamY     = GroundY - std::clamp(Instance.MountHeightMeters * Scale, 26.0f, 96.0f);
        const float RollRad  = Floating ? (Instance.CameraRollDegrees * 3.14159265f / 180.0f) : 0.0f;
        const float CosR     = std::cos(RollRad), SinR = std::sin(RollRad);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX = static_cast<float>(X) + 0.5f;
                float R = 0.028f, G = 0.030f, B = 0.035f;

                // Ground horizon line
                if (std::fabs(FY - GroundY) < 0.8f && FX > 14.0f && FX < 226.0f)
                {
                    R += 0.16f; G += 0.17f; B += 0.19f;
                }

                // Mount support (Tripod legs OR Floating drop-line + ground ellipse + motion arc)
                if (!Floating)
                {
                    const float DLeft  = SegmentDistance(FX, FY, CamX, CamY + 10.0f, CamX - 38.0f, GroundY);
                    const float DRight = SegmentDistance(FX, FY, CamX, CamY + 10.0f, CamX + 38.0f, GroundY);
                    const float DMid   = SegmentDistance(FX, FY, CamX, CamY + 10.0f, CamX + 6.0f,  GroundY);
                    if (DLeft < 1.5f || DRight < 1.5f) { R = 0.44f; G = 0.42f; B = 0.39f; }
                    else if (DMid < 1.3f) { R = 0.28f; G = 0.27f; B = 0.25f; }
                }
                else
                {
                    // Vertical dashed drop-line to ground
                    if (std::fabs(FX - CamX) < 0.9f && FY > CamY + 12.0f && FY < GroundY && (static_cast<int>(Y) % 8) < 4)
                    {
                        R += 0.36f; G += 0.54f; B += 0.72f;
                    }
                    // Ground projection ellipse
                    const float EX = (FX - CamX) / 24.0f, EY = (FY - GroundY) / 5.5f;
                    if (std::fabs(std::sqrt(EX * EX + EY * EY) - 1.0f) < 0.14f)
                    {
                        R += 0.36f; G += 0.54f; B += 0.72f;
                    }
                    // Curved crane/drone approach path from left
                    if (FX > 24.0f && FX < CamX - 20.0f)
                    {
                        const float T = (FX - 24.0f) / std::max(1.0f, (CamX - 44.0f));
                        const float ArcY = (CamY + 26.0f) * (1.0f - T) + CamY * T - std::sin(T * 3.14159f) * 22.0f;
                        if (std::fabs(FY - ArcY) < 1.0f && (static_cast<int>(X) % 7) < 4)
                        {
                            R += 0.28f; G += 0.44f; B += 0.62f;
                        }
                    }
                }

                // Height gauge bar on right
                if (std::fabs(FX - 208.0f) < 0.8f && FY >= CamY && FY <= GroundY)
                {
                    R = 0.72f; G = 0.56f; B = 0.28f;
                }
                if (FX >= 203.0f && FX <= 213.0f && (std::fabs(FY - CamY) < 0.8f || std::fabs(FY - GroundY) < 0.8f))
                {
                    R = 0.72f; G = 0.56f; B = 0.28f;
                }

                // Cinema camera body (rotated by Roll when Floating)
                const float DX = FX - CamX, DY = FY - CamY;
                const float LX =  DX * CosR + DY * SinR;
                const float LY = -DX * SinR + DY * CosR;
                if (std::fabs(LX) <= 16.0f && std::fabs(LY) <= 10.0f)
                {
                    R = 0.68f; G = 0.65f; B = 0.60f;
                }
                else if (LX > 16.0f && LX <= 29.0f && std::fabs(LY) <= 6.0f)
                {
                    R = 0.40f; G = 0.38f; B = 0.35f; // Lens matte box
                }
                else
                {
                    // Dual cinema film reels on top
                    const float R1 = std::sqrt((LX + 5.0f) * (LX + 5.0f) + (LY + 14.5f) * (LY + 14.5f));
                    const float R2 = std::sqrt((LX - 7.0f) * (LX - 7.0f) + (LY + 14.5f) * (LY + 14.5f));
                    if (std::fabs(R1 - 5.2f) < 1.5f || std::fabs(R2 - 5.2f) < 1.5f)
                    {
                        R = 0.68f; G = 0.65f; B = 0.60f;
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
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
    static constexpr std::array<const char*, 6u> Pages = {{ "Overview", "Lens", "Body", "Mount", "Motion blur", "Lens flare" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        UpdateLensSurface(Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 224.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Optical preview", Instance.TripodMounted ? "Tripod" : "Floating");
        LensPreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 142.0f }, 12.0f);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        const bool ReflOn = DisplaySettings ? DisplaySettings->ReflectionsEnabled : true;
        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Visible",     InspectorSwitchSymbol::CameraLens, Entry.VisibilityEnabled,    false },
            { "Lens flare",  InspectorSwitchSymbol::PostFx,     Instance.LensFlare.Enabled, false },
            { "Motion blur", InspectorSwitchSymbol::Cycle,      Instance.MotionBlurEnabled, false },
            { "Reflections", InspectorSwitchSymbol::Stars,      ReflOn,                     false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.LensFlare.Enabled = !Instance.LensFlare.Enabled; OutMutated = true; }
        else if (Clicked == 2) { Instance.MotionBlurEnabled = !Instance.MotionBlurEnabled; OutMutated = true; }
        else if (Clicked == 3 && DisplaySettings) { DisplaySettings->ReflectionsEnabled = !DisplaySettings->ReflectionsEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "camera", "optics" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 356.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Camera lens & gate");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Fl[16]{}; std::snprintf(Fl, sizeof(Fl), "%.0f", Instance.FocalLengthMm);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Fl, "mm", "Rectilinear optical projection");
        static constexpr std::array<const char*, 4u> Gates = {{ "Full 36mm", "Super 35", "Med 44mm", "APS-C" }};
        static constexpr std::array<float, 4u> GateW = {{ 36.0f, 24.89f, 43.8f, 23.5f }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Sensor gate", Gates, Instance.SensorGateIndex, 2u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 4) { Instance.SensorGateIndex = static_cast<uint32_t>(Chosen); Instance.SensorWidthMm = GateW[static_cast<std::size_t>(Chosen)]; OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7001u, "Focal length", "mm", "%.0f", 14.0f, 200.0f, Instance.FocalLengthMm, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7002u, "Aperture", "f/", "%.1f", 1.2f, 22.0f, Instance.ApertureFStop, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7003u, "Focus distance", "m", "%.1f", 0.3f, 120.0f, Instance.FocusDistanceMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 184.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Camera body");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        static constexpr std::array<const char*, 3u> Bodies = {{ "Cinema", "Rig", "Compact" }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Body rig", Bodies, Instance.RigPresetIndex, 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 3) { Instance.RigPresetIndex = static_cast<uint32_t>(Chosen); OutMutated = true; }
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "On tripod mount", Instance.TripodMounted, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        // F78 Mount page: On tripod (legs follow height) vs Floating (crane/rig/drone + roll) + Position & Aim at Showcase
        UpdateMountSurface(Instance);
        const bool Floating = !Instance.TripodMounted;
        const float MountSecH = Floating ? 430.0f : 378.0f;
        const Rectangle2D MountSec { LeftX, CursorY, InnerWidth, MountSecH };
        InspectorPrimitives::ConstructSectionSurface(Surface, MountSec, "Mount", Floating ? "B · Floating" : "A · On tripod");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        MountPreview.Present(Surface, { LeftX + PadX, Y, BodyW, 148.0f }, 12.0f);
        Y += 158.0f;
        static constexpr std::array<const char*, 2u> MountChoices = {{ "A · On tripod", "B · Floating" }};
        int32_t PickMount = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Camera mount", MountChoices, Floating ? 1u : 0u, 2u, Input, PickMount);
        if (PickMount >= 0)
        {
            Instance.TripodMounted = (PickMount == 0);
            if (Instance.TripodMounted && Instance.MountHeightMeters > 2.2f) Instance.MountHeightMeters = 2.2f;
            Entry.PositionY = Instance.MountHeightMeters;
            OutMutated = true;
        }
        const float MaxH = Floating ? 8.0f : 2.2f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(
            Surface, LeftX + PadX, Y, BodyW, 7010u, Floating ? "Height" : "Tripod height", "m", "%.2f",
            Floating ? 0.1f : 0.45f, MaxH, Instance.MountHeightMeters, ActiveDragId, Input, OutMutated);
        Entry.PositionY = Instance.MountHeightMeters;
        if (Floating)
        {
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7011u, "Roll", "deg", "%.0f", -45.0f, 45.0f, Instance.CameraRollDegrees, ActiveDragId, Input, OutMutated);
        }
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Aim at Showcase", Instance.AimAtShowcase, Input, OutMutated);
        CursorY = MountSec.Bottom() + InspectorMetrics::SectionGap;
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 7020u, ActiveDragId, Input, OutMutated);
    }
    else if (ActivePage == 4u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 212.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Motion blur");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable motion blur", Instance.MotionBlurEnabled, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7004u, "Shutter angle", "deg", "%.0f", 15.0f, 360.0f, Instance.ShutterAngleDegrees, ActiveDragId, Input, OutMutated, !Instance.MotionBlurEnabled);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        auto& Flare = Instance.LensFlare;
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Optical lens flare");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Lens flare", Flare.Enabled, Input, OutMutated);
        static constexpr std::array<const char*, 3u> Styles = {{ "Anamorphic", "Spherical", "Clean" }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Optical style", Styles, static_cast<uint32_t>(Flare.Style), 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 3) { Flare.Style = static_cast<GeometricRaster::LensFlareStyle>(Chosen); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7005u, "Intensity", "x", "%.2f", 0.0f, 3.0f, Flare.FlareIntensity, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7006u, "Halo glow", "x", "%.2f", 0.0f, 2.0f, Flare.HaloIntensity, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7007u, "Ghost elements", "x", "%.2f", 0.0f, 2.0f, Flare.GhostIntensity, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7008u, "Streak angle", "deg", "%.0f", -90.0f, 90.0f, Flare.StreakRotationDegrees, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
