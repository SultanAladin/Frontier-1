/*============================================================================================================================================
                                                        LEDLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LedLightInspector with own retained PCB die-array & optic preview

#include "Engine/Editor/Inspectors/LedLightInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}
} // namespace

void LedLightInspector::UpdatePackageSurface(const LedLightData& Instance)
{
    uint64_t Sig = 0x1F010001ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.DriverWatts);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.EfficacyLmPerW);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Dimmer);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.KelvinTemperature);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.PackageDiameterMm);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.OpticAngleDeg);

    const uint32_t W = 240u, H = 152u;
    PackagePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const float TR = Instance.RadiantTint.R * K.R;
        const float TG = Instance.RadiantTint.G * K.G;
        const float TB = Instance.RadiantTint.B * K.B;
        const float CX = static_cast<float>(W) * 0.5f;
        const float CY = static_cast<float>(H) * 0.52f;
        const float DieSize = std::clamp(58.0f + Instance.PackageDiameterMm * 0.28f, 60.0f, 88.0f);
        const float HalfDie = DieSize * 0.5f;
        const float HalfBoard = HalfDie + 10.0f;
        const float Dim = std::clamp(Instance.Dimmer, 0.0f, 1.0f);
        const float HalfOpticRad = (Instance.OpticAngleDeg * PiF) / 360.0f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float DY = static_cast<float>(Y) + 0.5f - CY;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX;
                const float AX = std::fabs(DX), AY = std::fabs(DY);
                const float D  = std::sqrt(DX * DX + DY * DY);
                float R = 0.024f, G = 0.026f, B = 0.030f;

                // Soft phosphor bloom glow
                const float Bloom = std::exp(-D * 0.028f) * 0.26f * Dim;
                R += TR * Bloom; G += TG * Bloom; B += TB * Bloom;

                // Metallic solder pins along left & right edges
                if (AX > HalfBoard && AX < HalfBoard + 7.0f && AY < HalfDie - 4.0f)
                {
                    const float PinT = (DY + HalfDie - 4.0f) / std::max(1.0f, (DieSize - 8.0f)) * 4.0f;
                    if (std::fabs(PinT - std::round(PinT)) < 0.14f)
                    {
                        R = 0.44f; G = 0.40f; B = 0.28f;
                    }
                }

                // Dark ceramic/PCB carrier housing
                if (AX <= HalfBoard && AY <= HalfBoard)
                {
                    R = 0.10f; G = 0.11f; B = 0.10f;
                    if (AX > HalfBoard - 1.2f || AY > HalfBoard - 1.2f)
                    {
                        R = 0.36f; G = 0.38f; B = 0.35f;
                    }
                }

                // Illuminated phosphor substrate + 4x4 LED die matrix
                if (AX <= HalfDie && AY <= HalfDie)
                {
                    const float CellPitch = DieSize * 0.25f;
                    const float LX = std::fmod(DX + HalfDie, CellPitch);
                    const float LY = std::fmod(DY + HalfDie, CellPitch);
                    const bool InsideCell = (LX > 2.6f && LX < CellPitch - 2.6f && LY > 2.6f && LY < CellPitch - 2.6f);
                    const float Gain = InsideCell ? (0.28f + 1.05f * Dim) : (0.14f + 0.34f * Dim);
                    R = 0.06f + TR * Gain;
                    G = 0.06f + TG * Gain;
                    B = 0.05f + TB * Gain;
                }

                // Emission optic angle arc above package
                const float ArcR = HalfDie * 1.48f;
                const float AngFromUp = std::atan2(DX, -DY);
                if (std::fabs(D - ArcR) < 1.15f && std::fabs(AngFromUp) <= HalfOpticRad)
                {
                    R += TR * 0.65f; G += TG * 0.65f; B += TB * 0.65f;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float LedLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LedLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Driver", "Optic", "Placement", "Shadows" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float EstLumens = std::max(0.0f, Instance.DriverWatts * Instance.EfficacyLmPerW * Instance.Dimmer);

    if (ActivePage == 0u)
    {
        UpdatePackageSurface(Instance);
        char CapBuf[56]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%.0f mm package · %.0f K · %.0f deg optic", Instance.PackageDiameterMm, Instance.KelvinTemperature, Instance.OpticAngleDeg);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "LED emitter", "COB Array");
        PackagePreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 152.0f }, 12.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 28.0f }, BodyW, 10.5f, InspectorMetrics::MutedInk, CapBuf);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char LmBuf[20]{}, WBuf[20]{}, EffBuf[20]{}, DimBuf[20]{};
        std::snprintf(LmBuf,  sizeof(LmBuf),  "%.0f", EstLumens);
        std::snprintf(WBuf,   sizeof(WBuf),   "%.1f", Instance.DriverWatts);
        std::snprintf(EffBuf, sizeof(EffBuf), "%.0f", Instance.EfficacyLmPerW);
        std::snprintf(DimBuf, sizeof(DimBuf), "%.0f", Instance.Dimmer * 100.0f);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { LmBuf,  "lm",   "EST. FLUX"    },
            { WBuf,   "W",    "DRIVER POWER" },
            { EffBuf, "lm/W", "EFFICACY"     },
            { DimBuf, "%",    "DIMMER"       },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",      InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled, false },
            { "Show emitter", InspectorSwitchSymbol::Cube,      Instance.ShowFixture,    false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.ShowFixture = !Instance.ShowFixture; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "led-cob" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 392.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Driver & colour");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char LmBuf[20]{}; std::snprintf(LmBuf, sizeof(LmBuf), "%.0f", EstLumens);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, LmBuf, "lm", "Estimated luminous flux");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8610u, "Driver power", "W", "%.1f", 0.1f, 100.0f, Instance.DriverWatts, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8611u, "Efficacy target", "lm/W", "%.0f", 10.0f, 250.0f, Instance.EfficacyLmPerW, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8612u, "Dimmer", "", "%.2f", 0.0f, 1.0f, Instance.Dimmer, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8613u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8614u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 218.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Package & optic");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char DimBuf[20]{}; std::snprintf(DimBuf, sizeof(DimBuf), "%.0f", Instance.Dimmer * 100.0f);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, DimBuf, "%", "Conversion dimmer output");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8620u, "Package diameter", "mm", "%.0f", 5.0f, 120.0f, Instance.PackageDiameterMm, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8621u, "Emission angle", "deg", "%.0f", 10.0f, 180.0f, Instance.OpticAngleDeg, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8640u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, false, "Draw emitter", Instance.ShowFixture, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
