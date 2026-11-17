/*============================================================================================================================================
                                                          CLOUDSINSPECTORVIEW.CPP
============================================================================================================================================*/
// 🧩 Native C++20 Volumetric Clouds & Local Cloud Volume Inspector implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Inspectors/CloudsInspectorView.h"
#include "Engine/Editor/Inspectors/CelestialInspectorWidgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Editor::Inspectors
{

namespace
{

constexpr std::array<const char*, 6u> CloudCategoryTitles = {{
    "Stratus",
    "Stratocumulus",
    "Cumulus",
    "Cumulonimbus",
    "Altostratus",
    "Cirrus",
}};

[[nodiscard]] const char* QueryCloudCategoryTitle(GeometricRaster::CloudCategoryType Category) noexcept
{
    const std::size_t Idx = std::min<std::size_t>(static_cast<std::size_t>(Category), 5u);
    return CloudCategoryTitles[Idx];
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER ENTRYPOINT
//------------------------------------------------------------------------------------------------------------------------

bool CloudsInspectorView::Render(
    PixelSpace&                   Surface,
    Rectangle2D                   Bounds,
    const CloudsInspectorTargets& Targets,
    const PaneInputReading&       Input,
    double                        DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Bounds.Width < 40.0f || Bounds.Height < 40.0f)
    {
        return false;
    }

    if (!Input.PrimaryPointer.Down)
    {
        ActiveSliderId    = -1;
        ActiveAxisDragId  = -1;
        DraggingDeckDatum = false;
    }

    if (Targets.ShowLocalVolumeOnly)
    {
        if (Targets.LocalVolume == nullptr)
        {
            return false;
        }
        return RenderLocalVolume(Surface, Bounds, *Targets.LocalVolume, Input);
    }

    if (Targets.Deck == nullptr)
    {
        return false;
    }
    return RenderGlobalDeck(Surface, Bounds, *Targets.Deck, Input);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             GLOBAL VOLUMETRIC CLOUD DECK
//------------------------------------------------------------------------------------------------------------------------

bool CloudsInspectorView::RenderGlobalDeck(
    PixelSpace&                              Surface,
    Rectangle2D                              Bounds,
    GeometricRaster::CloudDeckConfiguration& Deck,
    const PaneInputReading&                  Input)
{
    bool Mutated = false;

    Surface.FillRectangle(Bounds, CelestialSurfaceColors::RootBg);

    const float       PadX   = 12.0f;
    const float       InnerW = std::max(140.0f, Bounds.Width - PadX * 2.0f);
    const Rectangle2D ScrollViewport = Bounds;
    const Point2D     Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool        Interactive = ScrollViewport.Contains(Cursor) && !CloudTypeSheetOpen;

    Surface.PushClip(ScrollViewport);
    float CurY = Bounds.Y + 12.0f - ScrollOffsetY;
    const float StartY = CurY;
    const float LeftX  = Bounds.X + PadX;

    ConstructInspectorHeader(
        Surface,
        LeftX,
        CurY,
        InnerW,
        "Inspector / World / Clouds",
        "Volumetric Clouds",
        "Raymarched 3D FBM cloud deck, Beer-Powder scattering & wind advection");

    // 2-Column Capability Tiles: Enabled & Follow Wind
    {
        const float Gap   = 8.0f;
        const float TileW = (InnerW - Gap) * 0.5f;
        if (ConstructCapabilityTile(Surface, { LeftX, CurY, TileW, 38.0f }, "Enabled", "ON", "OFF", Deck.CloudsEnabled, Interactive, Input))
        {
            Mutated = true;
        }
        if (ConstructCapabilityTile(Surface, { LeftX + TileW + Gap, CurY, TileW, 38.0f }, "Follow Wind", "ON", "OFF", Deck.FollowWindEnabled, Interactive, Input))
        {
            Mutated = true;
        }
        CurY += 48.0f;
    }

    // Card 1: Cloud Category & Fractional Cover
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 240.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Cloud Category & Cover");
        Y += 24.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, CelestialSurfaceColors::InkMuted, "Cloud Type");
        Y += 15.0f;
        const Rectangle2D TypeBtn{ ColX, Y, ColW, 30.0f };
        if (ConstructSelectTrigger(Surface, TypeBtn, QueryCloudCategoryTitle(Deck.Category), CloudTypeSheetOpen, Interactive, Input.PrimaryPointer))
        {
            CloudTypeSheetOpen = true;
            CloudTypeTrigger   = TypeBtn;
        }
        Y += 38.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Cloud Fraction", "", 0.0f, 1.0f, Deck.CloudFraction, 2u, 300, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Density", "x", 0.0f, 4.0f, Deck.ExtinctionMultiplier, 2u, 301, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Feature Scale", "x", 0.20f, 3.00f, Deck.FeatureScaleMultiplier, 2u, 302, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 2: Altitude Deck Section
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 344.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Altitude Deck Section");
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 1.0f }, 9.0f, CelestialSurfaceColors::InkMuted, "Drag vertical datum");
        Y += 22.0f;

        char AltHero[24]{};
        char AltSub[48]{};
        std::snprintf(AltHero, sizeof(AltHero), "%.2f km", Deck.CloudFloorMeters * 0.001f);
        std::snprintf(AltSub, sizeof(AltSub), "floor · %.2f km thick", Deck.LayerThicknessMeters * 0.001f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { ColX, Y }, 19.0f, CelestialSurfaceColors::CloudMist, AltHero);
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 5.0f }, 10.0f, CelestialSurfaceColors::InkMuted, AltSub);
        Y += 28.0f;

        // Vertical Cross Section Diagram
        const Rectangle2D SectionRect{ ColX, Y, ColW, 118.0f };
        Surface.FillRectangle(SectionRect, CelestialSurfaceColors::RecessDark, 10.0f);
        Surface.StrokeRectangle(SectionRect, DraggingDeckDatum ? CelestialSurfaceColors::AmberAccent : CelestialSurfaceColors::SubtleBorder, 10.0f, 1.0f);

        const float MaxAltM = std::max(5000.0f, Deck.CloudFloorMeters + Deck.LayerThicknessMeters + 1200.0f);
        const float PlotTop = SectionRect.Y + 18.0f;
        const float PlotBot = SectionRect.Bottom() - 14.0f;
        const float PlotH   = std::max(24.0f, PlotBot - PlotTop);
        const float YFloor  = PlotBot - std::clamp(Deck.CloudFloorMeters / MaxAltM, 0.0f, 1.0f) * PlotH;
        const float YTop    = PlotBot - std::clamp((Deck.CloudFloorMeters + Deck.LayerThicknessMeters) / MaxAltM, 0.0f, 1.0f) * PlotH;
        const float BandH   = std::max(6.0f, YFloor - YTop);
        const uint8_t CloudAlpha = static_cast<uint8_t>(std::clamp(46.0f + Deck.ExtinctionMultiplier * 36.0f, 40.0f, 190.0f));

        Surface.FillRectangle({ SectionRect.X + 4.0f, YTop, SectionRect.Width - 8.0f, BandH }, ColorRgba{ 212, 224, 238, CloudAlpha }, 4.0f);
        Surface.StrokeLine({ SectionRect.X + 4.0f, YFloor }, { SectionRect.Right() - 4.0f, YFloor }, ColorRgba{ 255, 255, 255, 190 }, 1.2f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SectionRect.X + 10.0f, SectionRect.Y + 5.0f }, 8.0f, CelestialSurfaceColors::InkMuted, "CLOUD DECK · VERTICAL SECTION");
        char FloorLabel[32]{};
        std::snprintf(FloorLabel, sizeof(FloorLabel), "Floor: %.0f m", Deck.CloudFloorMeters);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SectionRect.X + 10.0f, std::max(PlotTop, YFloor - 13.0f) }, 8.5f, CelestialSurfaceColors::InkPrimary, FloorLabel);

        if (Interactive && SectionRect.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            DraggingDeckDatum = true;
            DeckDragStartY    = Cursor.Y;
            DeckOriginFloorM  = Deck.CloudFloorMeters;
        }
        if (DraggingDeckDatum && Input.PrimaryPointer.Down)
        {
            const float NextFloor = std::clamp(DeckOriginFloorM - (Cursor.Y - DeckDragStartY) * 38.0f, 100.0f, 12000.0f);
            if (std::fabs(NextFloor - Deck.CloudFloorMeters) > 0.5f)
            {
                Deck.CloudFloorMeters = NextFloor;
                Mutated               = true;
            }
        }
        Y += 128.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Layer Floor", "m", 100.0f, 12000.0f, Deck.CloudFloorMeters, 0u, 310, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Layer Thickness", "m", 100.0f, 6000.0f, Deck.LayerThicknessMeters, 0u, 311, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Deck Ceiling", "m", 4000.0f, 20000.0f, Deck.DeckCeilingMeters, 0u, 312, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 3: Scattering, Wind & Cloud Shadows
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 424.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Scattering, Wind & Cloud Shadows");
        Y += 24.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Anvil Top Spread", "", 0.0f, 1.0f, Deck.AnvilTopSpread, 2u, 320, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Mie Anisotropy (g)", "", -0.90f, 0.90f, Deck.ForwardScatterAnisotropy, 2u, 321, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Wind Speed", "m/s", 0.0f, 40.0f, Deck.WindSpeedMetersPerSecond, 1u, 322, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Wind Bearing", "deg", 0.0f, 360.0f, Deck.WindBearingDegrees, 0u, 323, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 50.0f;

        if (ConstructGreenSwitchRow(Surface, { ColX, Y, ColW, 34.0f }, "Cast Volumetric Cloud Shadows", Deck.CloudShadow.ShadowEnabled, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 42.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Shadow Fraction", "", 0.0f, 1.0f, Deck.CloudShadow.ShadowFraction, 2u, 324, ActiveSliderId, Interactive, Input, !Deck.CloudShadow.ShadowEnabled))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Shadow Density", "x", 0.0f, 6.0f, Deck.CloudShadow.ShadowDensityMultiplier, 2u, 325, ActiveSliderId, Interactive, Input, !Deck.CloudShadow.ShadowEnabled))
        {
            Mutated = true;
        }

        CurY += Box.Height + 12.0f;
    }

    const float ContentHeight = (CurY - StartY) + 16.0f;
    HandleInspectorScroll(Bounds, ContentHeight, ScrollOffsetY, LastContentH, Input);
    Surface.PopClip();

    if (CloudTypeSheetOpen)
    {
        uint32_t Chosen = static_cast<uint32_t>(Deck.Category);
        const Rectangle2D SheetRect = DropdownSelector::QuerySheetBounds(CloudTypeTrigger, static_cast<uint32_t>(CloudCategoryTitles.size()));
        const ControlContact Hit = DropdownSelector::ConstructOptionSheet(
            Surface,
            CloudTypeTrigger,
            CloudCategoryTitles.data(),
            static_cast<uint32_t>(CloudCategoryTitles.size()),
            Chosen,
            Input.PrimaryPointer,
            Chosen);
        if (Hit.Clicked)
        {
            const bool KeepEnabled = Deck.CloudsEnabled;
            GeometricRaster::VolumetricCloudSequence::ApplyCloudCategoryPreset(
                Deck,
                static_cast<GeometricRaster::CloudCategoryType>(std::min(Chosen, 5u)));
            Deck.CloudsEnabled = KeepEnabled;
            CloudTypeSheetOpen = false;
            Mutated            = true;
        }
        else if (Input.EscapePressed || (Input.PrimaryPointer.Pressed && !SheetRect.Contains(Cursor) && !CloudTypeTrigger.Contains(Cursor)))
        {
            CloudTypeSheetOpen = false;
        }
    }

    return Mutated;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              LOCAL CLOUD VOLUME (BOUNDED)
//------------------------------------------------------------------------------------------------------------------------

bool CloudsInspectorView::RenderLocalVolume(
    PixelSpace&                          Surface,
    Rectangle2D                          Bounds,
    GeometricRaster::BoundedCloudVolume& LocalVol,
    const PaneInputReading&              Input)
{
    bool Mutated = false;

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
        "Inspector / World / Local Cloud",
        "Local Cloud Volume",
        "Spatially bounded 3D cloud box in world meters");

    // 2-Column Capability Tiles: Enabled & Follow Wind
    {
        const float Gap   = 8.0f;
        const float TileW = (InnerW - Gap) * 0.5f;
        if (ConstructCapabilityTile(Surface, { LeftX, CurY, TileW, 38.0f }, "Enabled", "ON", "OFF", LocalVol.LocalEnabled, Interactive, Input))
        {
            Mutated = true;
        }
        if (ConstructCapabilityTile(Surface, { LeftX + TileW + Gap, CurY, TileW, 38.0f }, "Follow Wind", "ON", "OFF", LocalVol.FollowWindEnabled, Interactive, Input))
        {
            Mutated = true;
        }
        CurY += 48.0f;
    }

    // Card 1: World Space Box Bounds
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 148.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "World Space Box Bounds");
        Y += 22.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, CelestialSurfaceColors::InkMuted, "Centre (m)");
        Y += 15.0f;
        std::array<float, 3> CenterArr{ LocalVol.VolumeCenterMeters.X, LocalVol.VolumeCenterMeters.Y, LocalVol.VolumeCenterMeters.Z };
        if (ConstructAxisTripletRow(Surface, ColX, Y, ColW, CenterArr, -20000.0f, 20000.0f, 1.0f, 400, ActiveAxisDragId, AxisDragStartX, AxisDragOrigin, Interactive, Input))
        {
            LocalVol.VolumeCenterMeters = { CenterArr[0], CenterArr[1], CenterArr[2] };
            Mutated                     = true;
        }
        Y += 36.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, CelestialSurfaceColors::InkMuted, "Half Extent (m)");
        Y += 15.0f;
        std::array<float, 3> HalfArr{ LocalVol.HalfExtentMeters.X, LocalVol.HalfExtentMeters.Y, LocalVol.HalfExtentMeters.Z };
        if (ConstructAxisTripletRow(Surface, ColX, Y, ColW, HalfArr, 2.0f, 5000.0f, 1.0f, 410, ActiveAxisDragId, AxisDragStartX, AxisDragOrigin, Interactive, Input))
        {
            LocalVol.HalfExtentMeters = { HalfArr[0], HalfArr[1], HalfArr[2] };
            Mutated                   = true;
        }

        CurY += Box.Height + 10.0f;
    }

    // Card 2: Cloud Body
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 240.0f };
        ConstructInspectorGroupBox(Surface, Box);
        const float ColX = Box.X + 14.0f;
        const float ColW = Box.Width - 28.0f;
        float       Y    = Box.Y + 13.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 11.5f, CelestialSurfaceColors::InkHeading, "Cloud Body");
        Y += 24.0f;

        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Density", "x", 0.0f, 4.0f, LocalVol.ExtinctionMultiplier, 2u, 420, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Cloud Fraction", "", 0.0f, 1.0f, LocalVol.CloudFraction, 2u, 421, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Feature Scale", "m", 10.0f, 600.0f, LocalVol.FeatureScaleMeters, 0u, 422, ActiveSliderId, Interactive, Input))
        {
            Mutated = true;
        }
        Y += 49.0f;
        if (ConstructScalarSliderRow(Surface, ColX, Y, ColW, "Anisotropy (g)", "", -0.90f, 0.90f, LocalVol.ForwardScatterAnisotropy, 2u, 423, ActiveSliderId, Interactive, Input))
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
