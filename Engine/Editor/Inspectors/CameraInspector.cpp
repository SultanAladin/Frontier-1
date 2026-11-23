/*============================================================================================================================================
                                                           CAMERAINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Standalone CameraInspector — 1:1 pure-vector port of ProjectZeroEditor F78 (Overview OpticalGraphic + Mount Tripod/Floating + Position/Angle XYZ table)

#include "Engine/Editor/Inspectors/CameraInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float CameraInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, CameraInstanceData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    // Exact F78 5-page carousel (FollowUp68_CameraMountTripod.png: Page 2 / 5 Mount)
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Mount", "Focus", "Motion blur", "Effects" }};
    if (ActivePage >= Pages.size()) ActivePage = 0u;

    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const Point2D Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Clicked = Input.PrimaryPointer.Pressed;

    if (ActivePage == 0u)
    {
        //----------------------------------------------------------------------------------------------------------------
        // PAGE 1: OVERVIEW — OPTICS HERO + PURE-VECTOR OPTICAL SCHEMATIC (Exact FollowUp55_Camera-Overview.png)
        //----------------------------------------------------------------------------------------------------------------
        const Rectangle2D OptSec { LeftX, CursorY, InnerWidth, 478.0f };
        char GateCap[40]{};
        std::snprintf(GateCap, sizeof(GateCap), "%.0f mm · f/%.1f", Instance.FocalLengthMm, Instance.ApertureFStop);
        InspectorPrimitives::ConstructSectionSurface(Surface, OptSec, "Optics", GateCap);

        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char FlWhole[16]{};
        std::snprintf(FlWhole, sizeof(FlWhole), "%.0f", Instance.FocalLengthMm);
        Y = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, FlWhole, nullptr, "mm", "Rectilinear optical projection", 32.0f);

        const Rectangle2D SchemBox { LeftX + PadX, Y + 2.0f, BodyW, 128.0f };
        Surface.FillRectangle(SchemBox, ColorRgba{ 16u, 17u, 20u, 255u }, 12.0f);
        Surface.StrokeRectangle(SchemBox, ColorRgba{ 255u, 255u, 255u, 12u }, 12.0f, 1.0f);
        InspectorVector::ConstructOpticalSchematic(Surface, SchemBox, Instance.FocalLengthMm, Instance.SensorWidthMm, 1.778f, Instance.ApertureFStop);
        Y = SchemBox.Bottom() + 12.0f;

        static constexpr std::array<const char*, 4u> Gates = {{ "Full 36mm", "Super 35", "Med 44mm", "APS-C" }};
        static constexpr std::array<float, 4u>       GateW = {{ 36.0f, 24.89f, 43.8f, 23.5f }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Sensor gate", Gates, Instance.SensorGateIndex, 2u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 4)
        {
            Instance.SensorGateIndex = static_cast<uint32_t>(Chosen);
            Instance.SensorWidthMm   = GateW[static_cast<std::size_t>(Chosen)];
            OutMutated = true;
        }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7001u, "Focal length", "mm", "%.0f", 14.0f, 200.0f, Instance.FocalLengthMm, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7002u, "Aperture", "f/", "%.1f", 1.2f, 22.0f, Instance.ApertureFStop, ActiveDragId, Input, OutMutated);
        CursorY = OptSec.Bottom() + InspectorMetrics::SectionGap;

        // Quick controls
        const bool ReflOn = DisplaySettings ? DisplaySettings->ReflectionsEnabled : true;
        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Visible",     InspectorSwitchSymbol::CameraLens, Entry.VisibilityEnabled,    false },
            { "Lens flare",  InspectorSwitchSymbol::PostFx,     Instance.LensFlare.Enabled, false },
            { "Motion blur", InspectorSwitchSymbol::Cycle,      Instance.MotionBlurEnabled, false },
            { "Reflections", InspectorSwitchSymbol::Stars,      ReflOn,                     false },
        }};
        int32_t ClickedSw = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, ClickedSw);
        if (ClickedSw == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (ClickedSw == 1) { Instance.LensFlare.Enabled = !Instance.LensFlare.Enabled; OutMutated = true; }
        else if (ClickedSw == 2) { Instance.MotionBlurEnabled = !Instance.MotionBlurEnabled; OutMutated = true; }
        else if (ClickedSw == 3 && DisplaySettings) { DisplaySettings->ReflectionsEnabled = !DisplaySettings->ReflectionsEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "camera", "optics" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        //----------------------------------------------------------------------------------------------------------------
        // PAGE 2 / 5: MOUNT (Exact FollowUp68_CameraMountTripod.png & FollowUp68_CameraMountFloating.png)
        //----------------------------------------------------------------------------------------------------------------
        const bool Floating = !Instance.TripodMounted;
        const float StageH  = std::round(BodyW * (170.0f / 300.0f));
        const float ChoiceH = 88.0f;
        const float MountSecH = InspectorMetrics::SectionPadTop + 28.0f + StageH + 10.0f + ChoiceH + 12.0f + (Floating ? 116.0f : 60.0f) + InspectorMetrics::SectionPadBottom;
        const Rectangle2D MountSec { LeftX, CursorY, InnerWidth, MountSecH };
        char MountRight[32]{};
        std::snprintf(MountRight, sizeof(MountRight), "%s · %.2f m", Floating ? "Floating" : "Tripod", Instance.MountHeightMeters);
        InspectorPrimitives::ConstructSectionSurface(Surface, MountSec, "Mount", MountRight);

        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        // Dark navy hero stage (#141923) + verbatim MountPicture
        const Rectangle2D StageRect { LeftX + PadX, Y, BodyW, StageH };
        Surface.FillRectangle(StageRect, ColorRgba{ 20u, 25u, 35u, 255u }, 14.0f);
        Surface.StrokeRectangle(StageRect, ColorRgba{ 255u, 255u, 255u, 18u }, 14.0f, 1.0f);
        InspectorVector::ConstructCameraMountPicture(
            Surface, StageRect, Floating, Instance.MountHeightMeters, Entry.RotationX, Floating ? Instance.CameraRollDegrees : 0.0f, false);
        Y = StageRect.Bottom() + 10.0f;

        // 2-column visual choice cards: A · On tripod | B · Floating
        const float TileGap = 8.0f;
        const float TileW   = (BodyW - TileGap) * 0.5f;
        struct MountCardSpec { const char* Letter; const char* Title; const char* Desc; bool IsFloating; };
        static constexpr std::array<MountCardSpec, 2u> MountCards = {{
            { "A", "On tripod", "Legs follow height",  false },
            { "B", "Floating",  "Crane · rig · drone", true  },
        }};
        for (uint32_t I = 0u; I < 2u; ++I)
        {
            const Rectangle2D Tile { LeftX + PadX + static_cast<float>(I) * (TileW + TileGap), Y, TileW, ChoiceH };
            const bool Active = (Floating == MountCards[I].IsFloating);
            const bool Hov    = Tile.Contains(Cur);
            Surface.FillRectangle(Tile, Active ? ColorRgba{ 26u, 30u, 38u, 255u } : (Hov ? ColorRgba{ 24u, 25u, 28u, 255u } : ColorRgba{ 18u, 19u, 22u, 255u }), 12.0f);
            Surface.StrokeRectangle(Tile, Active ? ColorRgba{ 242u, 242u, 242u, 150u } : ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(Hov ? 36u : 16u) }, 12.0f, Active ? 1.3f : 1.0f);

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::SemiBold, { Tile.X + 8.0f, Tile.Y + 6.0f }, 9.5f,
                Active ? ColorRgba{ 255u, 59u, 48u, 255u } : InspectorMetrics::DimInk, MountCards[I].Letter);

            InspectorVector::ConstructCameraMountPicture(
                Surface, { Tile.X + 20.0f, Tile.Y + 4.0f, Tile.Width - 28.0f, 44.0f },
                MountCards[I].IsFloating, MountCards[I].IsFloating ? 2.1f : 1.35f, 0.0f, 0.0f, true);

            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Tile.X + 10.0f, Tile.Y + 52.0f }, 10.5f, Active ? ColorRgba{ 244u, 244u, 246u, 255u } : ColorRgba{ 216u, 218u, 224u, 255u }, MountCards[I].Title);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Tile.X + 10.0f, Tile.Y + 68.0f }, Tile.Width - 18.0f, 9.0f, InspectorMetrics::MutedInk, MountCards[I].Desc);

            if (Hov && Clicked && Floating != MountCards[I].IsFloating)
            {
                Instance.TripodMounted = !MountCards[I].IsFloating;
                if (Instance.TripodMounted && Instance.MountHeightMeters > 2.2f) Instance.MountHeightMeters = 2.2f;
                Entry.PositionY = Instance.MountHeightMeters;
                OutMutated = true;
            }
        }
        Y += ChoiceH + 12.0f;

        const float MaxH = Floating ? 8.0f : 2.2f;
        const float MinH = Floating ? 0.1f : 0.40f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(
            Surface, LeftX + PadX, Y, BodyW, 7010u, Floating ? "Height" : "Tripod height", "m", "%.2f",
            MinH, MaxH, Instance.MountHeightMeters, ActiveDragId, Input, OutMutated);
        Entry.PositionY = Instance.MountHeightMeters;
        if (Floating)
        {
            (void)InspectorPrimitives::ConstructScalarFieldRow(
                Surface, LeftX + PadX, Y, BodyW, 7011u, "Roll", "°", "%.0f", -45.0f, 45.0f, Instance.CameraRollDegrees, ActiveDragId, Input, OutMutated);
        }
        CursorY = MountSec.Bottom() + InspectorMetrics::SectionGap;

        //----------------------------------------------------------------------------------------------------------------
        // CARD 2: POSITION + ANGLE (Exact FollowUp68_CameraMountTripod.png — split "1.30 m high" + 3-col XYZ table + Aim)
        //----------------------------------------------------------------------------------------------------------------
        const Rectangle2D PosSec { LeftX, CursorY, InnerWidth, 336.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PosSec, "Position + angle", "WORLD");
        float PY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;

        const int32_t WholeH = static_cast<int32_t>(std::floor(std::max(0.0f, Entry.PositionY)));
        const int32_t CentH  = static_cast<int32_t>(std::lround((std::max(0.0f, Entry.PositionY) - static_cast<float>(WholeH)) * 100.0f)) % 100;
        char WhBuf[16]{}, FrBuf[16]{};
        std::snprintf(WhBuf, sizeof(WhBuf), "%d", WholeH);
        std::snprintf(FrBuf, sizeof(FrBuf), ".%02d", std::abs(CentH));
        PY = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, PY, WhBuf, FrBuf, "m high", nullptr, 28.0f);

        std::array<InspectorVector::TransformTableRow, 2u> Rows = {{
            { "Camera Position", "meters",  Entry.PositionX, Entry.PositionY, Entry.PositionZ, -120.0f, 120.0f, 0.05f, 2 },
            { "Camera Angle",    "degrees", Entry.RotationX, Entry.RotationY, Entry.RotationZ, -180.0f, 180.0f, 0.25f, 1 },
        }};
        int32_t ResetIdx = -1;
        bool TableMut = false;
        PY = InspectorVector::ConstructTransformTable(
            Surface, LeftX + PadX, PY + 4.0f, BodyW, "Transform", "Y-UP", Rows, 7200u, TransformDrag, Input, ResetIdx, TableMut);
        if (TableMut)
        {
            Entry.PositionX = Rows[0].X;
            Entry.PositionY = Rows[0].Y;
            Entry.PositionZ = Rows[0].Z;
            Instance.MountHeightMeters = std::clamp(Entry.PositionY, MinH, MaxH);
            Entry.RotationX = Rows[1].X;
            Entry.RotationY = Rows[1].Y;
            Entry.RotationZ = Rows[1].Z;
            OutMutated = true;
        }
        if (ResetIdx == 0)
        {
            Entry.PositionX = 0.0f; Entry.PositionY = 1.30f; Entry.PositionZ = 6.0f;
            Instance.MountHeightMeters = 1.30f;
            OutMutated = true;
        }
        else if (ResetIdx == 1)
        {
            Entry.RotationX = 0.0f; Entry.RotationY = 0.0f; Entry.RotationZ = 0.0f;
            Instance.CameraRollDegrees = 0.0f;
            OutMutated = true;
        }

        PY = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, PY + 2.0f, BodyW, "Aim at Showcase", Instance.AimAtShowcase, Input, OutMutated);

        // "↺ Back on its arc" action pill
        const Rectangle2D ArcBtn { LeftX + PadX, PY, BodyW, 28.0f };
        const bool ArcHov = ArcBtn.Contains(Cur);
        Surface.FillRectangle(ArcBtn, ArcHov ? ColorRgba{ 40u, 42u, 48u, 255u } : ColorRgba{ 26u, 27u, 30u, 255u }, 14.0f);
        Surface.StrokeRectangle(ArcBtn, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(ArcHov ? 36u : 16u) }, 14.0f, 1.0f);
        const float AW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, "Back on its arc");
        const float Ax = ArcBtn.Center().X - (AW + 14.0f) * 0.5f;
        InspectorVector::ConstructHtmlGlyph(Surface, InspectorVector::HtmlGlyph::ResetArc, { Ax + 5.0f, ArcBtn.Center().Y }, 11.0f, InspectorMetrics::MutedInk);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Ax + 15.0f, ArcBtn.Y + 8.0f }, 10.0f, InspectorMetrics::HeadingInk, "Back on its arc");
        if (ArcHov && Clicked)
        {
            Entry.PositionX = 0.0f; Entry.PositionY = 1.30f; Entry.PositionZ = 6.0f;
            Instance.MountHeightMeters = 1.30f;
            Instance.AimAtShowcase     = true;
            OutMutated = true;
        }
        CursorY = PosSec.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        //----------------------------------------------------------------------------------------------------------------
        // PAGE 3 / 5: FOCUS & CAMERA BODY
        //----------------------------------------------------------------------------------------------------------------
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 276.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Focus & body");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char DistBuf[16]{};
        std::snprintf(DistBuf, sizeof(DistBuf), "%.1f", Instance.FocusDistanceMeters);
        Y = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, DistBuf, nullptr, "m", "Focal plane distance", 28.0f);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y + 4.0f, BodyW, 7003u, "Focus distance", "m", "%.1f", 0.3f, 120.0f, Instance.FocusDistanceMeters, ActiveDragId, Input, OutMutated);
        static constexpr std::array<const char*, 3u> Bodies = {{ "Cinema", "Rig", "Compact" }};
        int32_t Chosen = -1;
        (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Body rig", Bodies, Instance.RigPresetIndex, 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 3) { Instance.RigPresetIndex = static_cast<uint32_t>(Chosen); OutMutated = true; }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        //----------------------------------------------------------------------------------------------------------------
        // PAGE 4 / 5: MOTION BLUR
        //----------------------------------------------------------------------------------------------------------------
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 212.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Motion blur");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable motion blur", Instance.MotionBlurEnabled, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7004u, "Shutter angle", "°", "%.0f", 15.0f, 360.0f, Instance.ShutterAngleDegrees, ActiveDragId, Input, OutMutated, !Instance.MotionBlurEnabled);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        //----------------------------------------------------------------------------------------------------------------
        // PAGE 5 / 5: EFFECTS (OPTICAL LENS FLARE)
        //----------------------------------------------------------------------------------------------------------------
        auto& Flare = Instance.LensFlare;
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Optical lens flare");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Lens flare", Flare.Enabled, Input, OutMutated);
        static constexpr std::array<const char*, 3u> Styles = {{ "Anamorphic", "Spherical", "Clean" }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Optical style", Styles, static_cast<uint32_t>(Flare.Style), 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 3) { Flare.Style = static_cast<GeometricRaster::LensFlareStyle>(Chosen); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7005u, "Intensity", "×", "%.2f", 0.0f, 3.0f, Flare.FlareIntensity, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7006u, "Halo glow", "×", "%.2f", 0.0f, 2.0f, Flare.HaloIntensity, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7007u, "Ghost elements", "×", "%.2f", 0.0f, 2.0f, Flare.GhostIntensity, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7008u, "Streak angle", "°", "%.0f", -90.0f, 90.0f, Flare.StreakRotationDegrees, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
