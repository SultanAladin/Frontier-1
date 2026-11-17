/*============================================================================================================================================
                                                          STARSINSPECTORVIEW.CPP
============================================================================================================================================*/
// 🧩 Native C++20 Stars Inspector implementation (Equatorial star chart, twinkle waveform monitor, celestial pole dial)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Inspectors/StarsInspectorView.h"
#include "Engine/Editor/Inspectors/CelestialInspectorWidgets.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Editor::Inspectors
{

namespace
{

[[nodiscard]] float HashStarUnit(uint32_t Seed, float Salt) noexcept
{
    const float S = std::sin(static_cast<float>(Seed) * 78.233f + Salt * 19.37f) * 43758.5453f;
    return S - std::floor(S);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER ENTRYPOINT
//------------------------------------------------------------------------------------------------------------------------

bool StarsInspectorView::Render(
    PixelSpace&                  Surface,
    Rectangle2D                  Bounds,
    const StarsInspectorTargets& Targets,
    const PaneInputReading&      Input,
    double                       DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Targets.Stars == nullptr || Bounds.Width < 40.0f || Bounds.Height < 40.0f)
    {
        return false;
    }

    auto& Stars   = *Targets.Stars;
    bool  Mutated = false;

    if (!Input.PrimaryPointer.Down)
    {
        ActiveSliderId = -1;
        DraggingPole   = false;
    }

    Surface.FillRectangle(Bounds, CelestialSurfaceColors::RootBg);

    const float       PadX   = 12.0f;
    const float       InnerW = std::max(140.0f, Bounds.Width - PadX * 2.0f);
    const Rectangle2D ScrollViewport = Bounds;
    const Point2D     Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool        Interactive = ScrollViewport.Contains(Cursor);

    Surface.PushClip(ScrollViewport);
    float CurY = Bounds.Y + 12.0f - ScrollOffsetY;
    const float StartY = CurY;
    const float LeftX  = Bounds.X + PadX;

    ConstructInspectorHeader(
        Surface,
        LeftX,
        CurY,
        InnerW,
        "Inspector / World / Stars",
        "Stars",
        "Deterministic nocturnal starfield, scintillation & celestial pole");

    // 2-Column Capability Tiles: Star Field & Twinkle
    {
        const float Gap   = 8.0f;
        const float TileW = (InnerW - Gap) * 0.5f;
        if (ConstructCapabilityTile(Surface, { LeftX, CurY, TileW, 38.0f }, "Star Field", "ON", "OFF", Stars.StarfieldEnabled, Interactive, Input))
        {
            Mutated = true;
        }
        if (ConstructCapabilityTile(Surface, { LeftX + TileW + Gap, CurY, TileW, 38.0f }, "Twinkle", "ON", "OFF", Stars.ScintillationEnabled, Interactive, Input))
        {
            Mutated = true;
        }
        CurY += 48.0f;
    }

    // Card 1: Equatorial Star Field
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 224.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Equatorial Star Field");
        Y += 22.0f;

        char MagHero[24]{};
        std::snprintf(MagHero, sizeof(MagHero), "%.1f", Stars.LimitingMagnitude);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { ColX, Y }, 20.0f, CelestialSurfaceColors::StarViolet, MagHero);
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 6.0f }, 10.0f, CelestialSurfaceColors::InkMuted, "mag limiting threshold");
        Y += 28.0f;

        // Equatorial Starfield Chart
        const Rectangle2D ChartRect{ ColX, Y, ColW, 96.0f };
        Surface.FillRectangle(ChartRect, CelestialSurfaceColors::RecessDark, 10.0f);
        Surface.StrokeRectangle(ChartRect, CelestialSurfaceColors::SubtleBorder, 10.0f, 1.0f);
        const float RotRad = Stars.CelestialRotationDegrees * Constants::DegToRadFloat;
        const float CosR   = std::cos(RotRad);
        const float SinR   = std::sin(RotRad);
        for (uint32_t SIdx = 0u; SIdx < 120u; ++SIdx)
        {
            const float Rx = HashStarUnit(SIdx, 1.3f) - 0.5f;
            const float Ry = HashStarUnit(SIdx, 7.9f) - 0.5f;
            const float Px = ChartRect.Center().X + (Rx * CosR - Ry * SinR) * (ChartRect.Width - 16.0f);
            const float Py = ChartRect.Center().Y + (Rx * SinR + Ry * CosR) * (ChartRect.Height - 16.0f);
            if (!ChartRect.Contains({ Px, Py }))
            {
                continue;
            }
            const float MagRequirement = static_cast<float>(SIdx % 8u);
            const bool  VisibleStar    = Stars.StarfieldEnabled && (MagRequirement <= Stars.LimitingMagnitude);
            const uint8_t Alpha        = VisibleStar ? static_cast<uint8_t>(std::clamp(110.0f + 135.0f * HashStarUnit(SIdx, 3.7f), 60.0f, 245.0f)) : 16u;
            const float   Radius       = std::clamp((0.55f + 0.9f * HashStarUnit(SIdx, 5.1f)) * Stars.PointSizeScale, 0.5f, 2.5f);
            Surface.FillCircle({ Px, Py }, Radius, ColorRgba{ 214, 200, 239, Alpha });
        }
        Y += 106.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Limiting Magnitude", "mag", 0.0f, 8.0f, Stars.LimitingMagnitude, 1u, 200, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 2: Scintillation / Twinkle Monitor
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 212.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        const float DepthFrac = std::clamp(Stars.ScintillationDepthPercent * 0.01f, 0.0f, 1.0f);
        const float InstantFlux = (Stars.StarfieldEnabled && Stars.ScintillationEnabled)
            ? (1.0f - DepthFrac * (0.5f + 0.3f * std::sin(Targets.SimulationSeconds * Constants::TauFloat * Stars.ScintillationRateHertz)))
            : 1.0f;
        char FluxLabel[24]{};
        std::snprintf(FluxLabel, sizeof(FluxLabel), "%d%% flux", static_cast<int>(std::lround(InstantFlux * 100.0f)));
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Scintillation Monitor");
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::SemiBold, { ColX + ColW, Y + 1.0f }, 9.5f, CelestialSurfaceColors::StarViolet, FluxLabel);
        Y += 24.0f;

        const Rectangle2D WaveRect{ ColX, Y, ColW, 66.0f };
        Surface.FillRectangle(WaveRect, CelestialSurfaceColors::RecessDark, 10.0f);
        Surface.StrokeRectangle(WaveRect, CelestialSurfaceColors::SubtleBorder, 10.0f, 1.0f);

        constexpr uint32_t WavePts = 48u;
        Point2D PrevPt{};
        for (uint32_t WIdx = 0u; WIdx <= WavePts; ++WIdx)
        {
            const float T = static_cast<float>(WIdx) / static_cast<float>(WavePts);
            const float X = WaveRect.X + 10.0f + T * (WaveRect.Width - 20.0f);
            float Mod = 1.0f;
            if (Stars.StarfieldEnabled && Stars.ScintillationEnabled)
            {
                const float Phase = (Targets.SimulationSeconds + T * 2.0f) * Constants::TauFloat * Stars.ScintillationRateHertz;
                Mod = 1.0f - DepthFrac * (0.45f + 0.35f * std::sin(Phase) + 0.18f * std::cos(Phase * 2.3f));
            }
            const float YWave = WaveRect.Bottom() - 10.0f - std::clamp(Mod, 0.0f, 1.0f) * (WaveRect.Height - 20.0f);
            const Point2D CurPt{ X, YWave };
            if (WIdx > 0u)
            {
                Surface.StrokeLine(PrevPt, CurPt, CelestialSurfaceColors::StarViolet, 1.6f);
            }
            PrevPt = CurPt;
        }
        Y += 76.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Twinkle Depth", "%", 0.0f, 100.0f, Stars.ScintillationDepthPercent, 0u, 210, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Twinkle Rate", "Hz", 0.20f, 3.00f, Stars.ScintillationRateHertz, 2u, 211, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 3: Celestial Pole & Renderer Scale
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 316.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Celestial Pole & Renderer Scale");
        Y += 24.0f;

        const Rectangle2D DialRect{ ColX, Y, ColW, 118.0f };
        Surface.FillRectangle(DialRect, CelestialSurfaceColors::RecessDark, 10.0f);
        Surface.StrokeRectangle(DialRect, DraggingPole ? CelestialSurfaceColors::AmberAccent : CelestialSurfaceColors::SubtleBorder, 10.0f, 1.0f);

        const Point2D Ctr{ DialRect.Center().X, DialRect.Center().Y - 5.0f };
        const float   R = 40.0f;
        Surface.StrokeCircle(Ctr, R, ColorRgba{ 179, 155, 204, 72 }, 1.2f);
        Surface.StrokeCircle(Ctr, R * 0.55f, ColorRgba{ 179, 155, 204, 42 }, 1.0f);
        const float   PoleRad = (Stars.CelestialRotationDegrees - 90.0f) * Constants::DegToRadFloat;
        const Point2D HandPt{ Ctr.X + R * std::cos(PoleRad), Ctr.Y + R * std::sin(PoleRad) };
        Surface.StrokeLine(Ctr, HandPt, ColorRgba{ 200, 164, 223, 175 }, 1.5f);
        Surface.FillCircle(Ctr, 3.0f, ColorRgba{ 228, 210, 247, 255 });
        Surface.FillCircle(HandPt, 5.0f, ColorRgba{ 47, 37, 56, 255 });
        Surface.StrokeCircle(HandPt, 5.0f, ColorRgba{ 228, 210, 247, 255 }, 1.3f);

        char PoleCaption[48]{};
        std::snprintf(PoleCaption, sizeof(PoleCaption), "Pole Rotation: %.1f°", Stars.CelestialRotationDegrees);
        const float Tw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, PoleCaption);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { DialRect.Center().X - Tw * 0.5f, DialRect.Bottom() - 15.0f },
            9.0f,
            CelestialSurfaceColors::InkMuted,
            PoleCaption);

        if (Interactive && DialRect.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            DraggingPole = true;
        }
        if (DraggingPole && Input.PrimaryPointer.Down)
        {
            float Deg = std::atan2(Cursor.Y - Ctr.Y, Cursor.X - Ctr.X) * Constants::RadToDegFloat + 90.0f;
            if (Deg < 0.0f)
            {
                Deg += 360.0f;
            }
            Stars.CelestialRotationDegrees = std::fmod(Deg, 360.0f);
            Mutated                        = true;
        }
        Y += 128.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Celestial Rotation", "deg", 0.0f, 360.0f, Stars.CelestialRotationDegrees, 1u, 220, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Brightness", "x", 0.0f, 4.0f, Stars.BrightnessMultiplier, 2u, 221, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Point Size", "x", 0.40f, 3.00f, Stars.PointSizeScale, 2u, 222, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }

        CurY += Box.Height + 12.0f;
    }

    const float ContentHeight = (CurY - StartY) + 16.0f;
    HandleInspectorScroll(Bounds, ContentHeight, ScrollOffsetY, LastContentH, Input);
    Surface.PopClip();

    return Mutated;
}

} // namespace Frontier::Editor::Inspectors
