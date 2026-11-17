/*============================================================================================================================================
                                                           MOONINSPECTORVIEW.CPP
============================================================================================================================================*/
// 🧩 Native C++20 Moon Inspector implementation (4 slots, 6 surface presets, synodic phase disc, Az/El sky pad, roll/pitch)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Inspectors/MoonInspectorView.h"
#include "Engine/Editor/Inspectors/CelestialInspectorWidgets.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier::Editor::Inspectors
{

namespace
{

struct MoonSurfacePresetDescriptor
{
    const char* Title;
    ColorRgba   HighlightTint;
    ColorRgba   ShadowCrater;
};

constexpr std::array<MoonSurfacePresetDescriptor, 6u> MoonPresetDescriptors = {{
    { "Luna",    { 216, 224, 232, 255 }, { 112, 118, 128, 255 } },
    { "Ember",   { 255, 168, 108, 255 }, { 112, 48,  32,  255 } },
    { "Glacier", { 196, 236, 255, 255 }, { 84,  138, 174, 255 } },
    { "Sulfur",  { 244, 222, 124, 255 }, { 138, 114, 42,  255 } },
    { "Shroud",  { 146, 152, 166, 255 }, { 56,  60,  72,  255 } },
    { "Shard",   { 168, 234, 212, 255 }, { 64,  126, 110, 255 } },
}};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER ENTRYPOINT
//------------------------------------------------------------------------------------------------------------------------

bool MoonInspectorView::Render(
    PixelSpace&                 Surface,
    Rectangle2D                 Bounds,
    const MoonInspectorTargets& Targets,
    const PaneInputReading&     Input,
    double                      DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Targets.Moons == nullptr || Targets.ActiveSlot == nullptr || Bounds.Width < 40.0f || Bounds.Height < 40.0f)
    {
        return false;
    }

    uint32_t& SlotIdx = *Targets.ActiveSlot;
    SlotIdx           = std::min(SlotIdx, 3u);
    auto& Moon        = (*Targets.Moons)[SlotIdx];
    bool  Mutated     = false;

    if (!Input.PrimaryPointer.Down)
    {
        ActiveSliderId = -1;
        DraggingSkyPad = false;
    }

    Surface.FillRectangle(Bounds, CelestialSurfaceColors::RootBg);

    const float       PadX   = 12.0f;
    const float       InnerW = std::max(140.0f, Bounds.Width - PadX * 2.0f);
    const Rectangle2D ScrollViewport = Bounds;
    const Point2D     Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool        Interactive = ScrollViewport.Contains(Cursor) && !SlotSheetOpen;

    Surface.PushClip(ScrollViewport);
    float CurY = Bounds.Y + 12.0f - ScrollOffsetY;
    const float StartY = CurY;
    const float LeftX  = Bounds.X + PadX;

    ConstructInspectorHeader(
        Surface,
        LeftX,
        CurY,
        InnerW,
        "Inspector / World / Moon",
        "Moon",
        "Multi-instance lunar discs, 29.53-day phase & 6-preset surface atlas");

    // Card 1: Moon Instance & Switches
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 134.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Moon Instance & Switches");
        Y += 24.0f;

        const std::size_t PresetOrd = std::min<std::size_t>(static_cast<std::size_t>(Moon.SurfacePreset), 5u);
        char SlotTitle[64]{};
        std::snprintf(SlotTitle, sizeof(SlotTitle), "Slot %u · %s", SlotIdx, MoonPresetDescriptors[PresetOrd].Title);
        const Rectangle2D SlotBtn{ ColX, Y, ColW, 30.0f };
        if (ConstructSelectTrigger(Surface, SlotBtn, SlotTitle, SlotSheetOpen, Interactive, Input.PrimaryPointer))
        {
            SlotSheetOpen   = true;
            SlotTriggerRect = SlotBtn;
        }
        Y += 40.0f;

        const float Gap   = 8.0f;
        const float TileW = (ColW - Gap) * 0.5f;
        if (ConstructCapabilityTile(Surface, { ColX, Y, TileW, 38.0f }, "Visible", "ON", "OFF", Moon.Visible, Interactive, Input))
        {
            Mutated = true;
        }
        if (ConstructCapabilityTile(Surface, { ColX + TileW + Gap, Y, TileW, 38.0f }, "Follow Sky", "LOCKED", "FREE", Moon.FollowSkyOppositeSun, Interactive, Input))
        {
            Mutated = true;
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 2: Lunar Phase
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 140.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Lunar Phase");
        Y += 22.0f;

        const float IllumFraction = 0.5f * (1.0f - std::cos(Moon.SynodicPhaseFraction * Constants::TauFloat));
        const int   IllumPercent  = static_cast<int>(std::lround(IllumFraction * 100.0f));
        char DayHero[24]{};
        char DayMeta[48]{};
        std::snprintf(DayHero, sizeof(DayHero), "%.1f d", Moon.SynodicPhaseFraction * 29.53f);
        std::snprintf(DayMeta, sizeof(DayMeta), "%d%% illuminated", IllumPercent);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { ColX, Y }, 20.0f, CelestialSurfaceColors::LunarBlue, DayHero);

        // Miniature live phase preview disc
        const std::size_t PresetOrd = std::min<std::size_t>(static_cast<std::size_t>(Moon.SurfacePreset), 5u);
        const Point2D DiscCtr{ ColX + ColW - 16.0f, Y + 10.0f };
        Surface.FillCircle(DiscCtr, 11.0f, ColorRgba{ 24, 28, 36, 255 });
        const float LitRadius = std::max(1.5f, 10.5f * IllumFraction);
        const float PhaseShift = (Moon.SynodicPhaseFraction - 0.5f) * -8.0f;
        Surface.FillCircle({ DiscCtr.X + PhaseShift * 0.4f, DiscCtr.Y }, LitRadius, MoonPresetDescriptors[PresetOrd].HighlightTint);
        Surface.StrokeCircle(DiscCtr, 11.5f, CelestialSurfaceColors::SubtleBorder, 1.0f);

        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { DiscCtr.X - 18.0f, Y + 5.0f }, 10.0f, CelestialSurfaceColors::InkMuted, DayMeta);
        Y += 32.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Synodic Phase", "", 0.0f, 1.0f, Moon.SynodicPhaseFraction, 2u, 100, ActiveSliderId, Interactive, Input, Moon.FollowSkyOppositeSun))
        {
            Mutated = true;
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 3: Moon Surface Preset Gallery (2x3 grid)
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 134.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Moon Surface Preset Gallery");
        Y += 24.0f;

        const float Gap   = 7.0f;
        const float BtnW  = (ColW - Gap * 2.0f) / 3.0f;
        const float BtnH  = 36.0f;
        for (std::size_t PIdx = 0u; PIdx < MoonPresetDescriptors.size(); ++PIdx)
        {
            const std::size_t Col = PIdx % 3u;
            const std::size_t Row = PIdx / 3u;
            const Rectangle2D Btn{ ColX + static_cast<float>(Col) * (BtnW + Gap), Y + static_cast<float>(Row) * (BtnH + Gap), BtnW, BtnH };
            const bool        Selected = (static_cast<std::size_t>(Moon.SurfacePreset) == PIdx);
            const bool        Hovered  = Interactive && Btn.Contains(Cursor);

            Surface.FillRectangle(Btn, Selected ? ColorRgba{ 42, 38, 30, 255 } : (Hovered ? ColorRgba{ 30, 30, 34, 255 } : CelestialSurfaceColors::SubWellBg), 10.0f);
            Surface.StrokeRectangle(Btn, Selected ? CelestialSurfaceColors::AmberAccent : CelestialSurfaceColors::HairlineBorder, 10.0f, Selected ? 1.5f : 1.0f);

            const Point2D SwatchCtr{ Btn.X + 15.0f, Btn.Center().Y };
            Surface.FillCircle(SwatchCtr, 7.5f, MoonPresetDescriptors[PIdx].ShadowCrater);
            Surface.FillCircle({ SwatchCtr.X - 1.5f, SwatchCtr.Y - 1.5f }, 5.5f, MoonPresetDescriptors[PIdx].HighlightTint);
            Surface.StrokeCircle(SwatchCtr, 7.5f, ColorRgba{ 255, 255, 255, 36 }, 1.0f);

            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Medium,
                { Btn.X + 27.0f, Btn.Y + 11.5f },
                std::max(24.0f, Btn.Width - 32.0f),
                10.0f,
                Selected ? CelestialSurfaceColors::InkPrimary : CelestialSurfaceColors::InkLabel,
                MoonPresetDescriptors[PIdx].Title);

            if (Hovered && Input.PrimaryPointer.Pressed)
            {
                Moon.SurfacePreset = static_cast<GeometricRaster::LunarSurfacePreset>(PIdx);
                Mutated            = true;
            }
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 4: Moonlight & Angular Scale
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 224.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Moonlight & Angular Scale");
        Y += 24.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Brightness", "x", 0.0f, 6.0f, Moon.BrightnessMultiplier, 2u, 110, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Halo Glow", "x", 0.0f, 3.0f, Moon.HaloGlowMultiplier, 2u, 111, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Angular Size", "deg", 0.10f, 180.0f, Moon.AngularDiameterDegrees, 2u, 112, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 50.0f;

        const Rectangle2D ResetBtn{ ColX, Y, ColW, 28.0f };
        const bool Hovered = Interactive && ResetBtn.Contains(Cursor);
        Surface.FillRectangle(ResetBtn, Hovered ? CelestialSurfaceColors::TabHoverBg : CelestialSurfaceColors::TabInactiveBg, 14.0f);
        Surface.StrokeRectangle(ResetBtn, CelestialSurfaceColors::SubtleBorder, 14.0f, 1.0f);
        const char* ResetLabel = "Natural Angular Diameter · 0.52°";
        const float Tw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.0f, ResetLabel);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { ResetBtn.X + std::max(8.0f, (ResetBtn.Width - Tw) * 0.5f), ResetBtn.Y + 7.5f },
            10.0f,
            CelestialSurfaceColors::InkHeading,
            ResetLabel);
        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            Moon.AngularDiameterDegrees = 0.52f;
            Mutated                     = true;
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 5: Lunar Position & Orientation
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 384.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Lunar Position & Orientation");
        Y += 24.0f;

        // Interactive 2D Azimuth/Elevation Crosshair Pad
        const Rectangle2D PadRect{ ColX, Y, ColW, 132.0f };
        Surface.FillRectangle(PadRect, CelestialSurfaceColors::RecessDark, 10.0f);
        Surface.StrokeRectangle(PadRect, DraggingSkyPad ? CelestialSurfaceColors::AmberAccent : CelestialSurfaceColors::SubtleBorder, 10.0f, 1.0f);

        const float GroundY = PadRect.Center().Y;
        Surface.StrokeLine({ PadRect.X + 8.0f, GroundY }, { PadRect.Right() - 8.0f, GroundY }, ColorRgba{ 127, 149, 176, 120 }, 1.1f);
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { PadRect.Right() - 10.0f, GroundY - 13.0f }, 8.0f, ColorRgba{ 111, 128, 150, 200 }, "GROUND 0°");

        const float NormX = std::clamp(Moon.AzimuthDegrees / 360.0f, 0.0f, 1.0f);
        const float NormY = std::clamp((90.0f - Moon.ElevationDegrees) / 180.0f, 0.0f, 1.0f);
        const float MarkerX = PadRect.X + 14.0f + NormX * (PadRect.Width - 28.0f);
        const float MarkerY = PadRect.Y + 12.0f + NormY * (PadRect.Height - 24.0f);

        Surface.StrokeLine({ MarkerX, PadRect.Y + 6.0f }, { MarkerX, PadRect.Bottom() - 6.0f }, ColorRgba{ 157, 189, 214, 75 }, 1.0f);
        Surface.StrokeLine({ PadRect.X + 6.0f, MarkerY }, { PadRect.Right() - 6.0f, MarkerY }, ColorRgba{ 157, 189, 214, 75 }, 1.0f);
        const std::size_t PresetOrd = std::min<std::size_t>(static_cast<std::size_t>(Moon.SurfacePreset), 5u);
        Surface.FillCircle({ MarkerX, MarkerY }, 7.5f, MoonPresetDescriptors[PresetOrd].HighlightTint);
        Surface.StrokeCircle({ MarkerX, MarkerY }, 8.0f, ColorRgba{ 255, 255, 255, 90 }, 1.0f);

        if (Interactive && !Moon.FollowSkyOppositeSun && PadRect.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            DraggingSkyPad = true;
        }
        if (DraggingSkyPad && Input.PrimaryPointer.Down && !Moon.FollowSkyOppositeSun)
        {
            const float U = std::clamp((Cursor.X - (PadRect.X + 14.0f)) / std::max(1.0f, PadRect.Width - 28.0f), 0.0f, 1.0f);
            const float V = std::clamp((Cursor.Y - (PadRect.Y + 12.0f)) / std::max(1.0f, PadRect.Height - 24.0f), 0.0f, 1.0f);
            Moon.AzimuthDegrees   = U * 360.0f;
            Moon.ElevationDegrees = 90.0f - V * 180.0f;
            Mutated               = true;
        }
        Y += 142.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Azimuth", "deg", 0.0f, 360.0f, Moon.AzimuthDegrees, 0u, 120, ActiveSliderId, Interactive, Input, Moon.FollowSkyOppositeSun))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Elevation", "deg", -90.0f, 90.0f, Moon.ElevationDegrees, 0u, 121, ActiveSliderId, Interactive, Input, Moon.FollowSkyOppositeSun))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Surface Roll", "deg", 0.0f, 360.0f, Moon.SurfaceRollDegrees, 0u, 122, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Surface Pitch", "deg", -180.0f, 180.0f, Moon.SurfacePitchDegrees, 0u, 123, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }

        CurY += Box.Height + 12.0f;
    }

    const float ContentHeight = (CurY - StartY) + 16.0f;
    HandleInspectorScroll(Bounds, ContentHeight, ScrollOffsetY, LastContentH, Input);
    Surface.PopClip();

    if (SlotSheetOpen)
    {
        std::array<std::string, 4u> SlotLabels{};
        std::array<const char*, 4u> Opts{};
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const std::size_t P = std::min<std::size_t>(static_cast<std::size_t>((*Targets.Moons)[I].SurfacePreset), 5u);
            char Buf[48]{};
            std::snprintf(Buf, sizeof(Buf), "Slot %u · %s%s", I, MoonPresetDescriptors[P].Title, (*Targets.Moons)[I].Visible ? " (Visible)" : "");
            SlotLabels[I] = Buf;
            Opts[I]       = SlotLabels[I].c_str();
        }
        uint32_t Chosen = SlotIdx;
        const Rectangle2D SheetRect = DropdownSelector::QuerySheetBounds(SlotTriggerRect, static_cast<uint32_t>(Opts.size()));
        const ControlContact Hit = DropdownSelector::ConstructOptionSheet(
            Surface,
            SlotTriggerRect,
            Opts.data(),
            static_cast<uint32_t>(Opts.size()),
            Chosen,
            Input.PrimaryPointer,
            Chosen);
        if (Hit.Clicked)
        {
            SlotIdx       = std::min(Chosen, 3u);
            SlotSheetOpen = false;
            Mutated       = true;
        }
        else if (Input.EscapePressed || (Input.PrimaryPointer.Pressed && !SheetRect.Contains(Cursor) && !SlotTriggerRect.Contains(Cursor)))
        {
            SlotSheetOpen = false;
        }
    }

    return Mutated;
}

} // namespace Frontier::Editor::Inspectors
