/*============================================================================================================================================
                                                           EXECUTIONMODESTRIP.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable ExecutionModeStrip (Edit / Simulate / Play-Pause pill strip with optional Rewind trigger)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ExecutionModeStrip.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <array>

namespace Frontier
{

namespace
{

constexpr float EditCapsuleWidth     = 58.0f;
constexpr float SimulateCapsuleWidth = 72.0f;
constexpr float PlayCapsuleWidth     = 64.0f;
constexpr float PillStripWidth       = 6.0f + EditCapsuleWidth + 2.0f + SimulateCapsuleWidth + 2.0f + PlayCapsuleWidth;
constexpr float RewindButtonWidth    = 34.0f;
constexpr float RewindGapWidth       = 8.0f;

ColorRgba ScaleAlpha(ColorRgba Base, float Opacity) noexcept
{
    return Base.ScaledAlpha(Opacity);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 PUBLIC INTERFACE
//------------------------------------------------------------------------------------------------------------------------

float ExecutionModeStrip::PreferredStripWidth(bool ShowRewind) noexcept
{
    return ShowRewind ? (PillStripWidth + RewindButtonWidth + RewindGapWidth) : PillStripWidth;
}

ExecutionModeStripResult ExecutionModeStrip::Construct(
    PixelSpace&                      Surface,
    Rectangle2D                      Bounds,
    const ExecutionModeStripReading& Reading,
    const ControlPointer&            Pointer,
    float                            Opacity) noexcept
{
    ExecutionModeStripResult Result {};
    const Point2D            Cursor{ Pointer.X, Pointer.Y };

    float StripX = Bounds.X;
    if (Reading.ShowRewindButton)
    {
        const Rectangle2D RewindBtn{ Bounds.X, Bounds.Y, RewindButtonWidth, RecommendedHeightPx };
        const bool        RewindHover = !Reading.OverlayBlockingInput && RewindBtn.Contains(Cursor);

        Surface.FillRectangle(
            RewindBtn,
            ScaleAlpha(RewindHover ? ColorRgba{ 44, 44, 44, 255 } : ColorRgba{ 26, 26, 26, 255 }, Opacity),
            15.5f);
        Surface.StrokeRectangle(RewindBtn, ScaleAlpha(ColorRgba{ 255, 255, 255, 20 }, Opacity), 15.5f, 1.0f);

        const ColorRgba RewindInk = ScaleAlpha(
            RewindHover ? ColorRgba{ 236, 236, 236, 255 } : ColorRgba{ 165, 165, 170, 255 },
            Opacity);
        Surface.StrokeLine(
            { RewindBtn.X + 10.0f, RewindBtn.Y + 10.0f },
            { RewindBtn.X + 10.0f, RewindBtn.Bottom() - 10.0f },
            RewindInk,
            1.6f);
        const std::array<Point2D, 3> Tri1 = {{
            { RewindBtn.X + 11.5f, RewindBtn.Center().Y },
            { RewindBtn.X + 18.0f, RewindBtn.Y + 10.0f },
            { RewindBtn.X + 18.0f, RewindBtn.Bottom() - 10.0f },
        }};
        const std::array<Point2D, 3> Tri2 = {{
            { RewindBtn.X + 18.0f, RewindBtn.Center().Y },
            { RewindBtn.X + 24.5f, RewindBtn.Y + 10.0f },
            { RewindBtn.X + 24.5f, RewindBtn.Bottom() - 10.0f },
        }};
        Surface.FillPolygon(Tri1, RewindInk);
        Surface.FillPolygon(Tri2, RewindInk);

        if (RewindHover && Pointer.Pressed)
        {
            Result.RewindPressed = true;
        }
        StripX += RewindButtonWidth + RewindGapWidth;
    }

    const Rectangle2D ModeStrip{ StripX, Bounds.Y, PillStripWidth, RecommendedHeightPx };
    Surface.FillRectangle(ModeStrip, ScaleAlpha(ColorRgba{ 9, 9, 9, 255 }, Opacity), 15.5f);
    Surface.StrokeRectangle(ModeStrip, ScaleAlpha(ColorRgba{ 255, 255, 255, 18 }, Opacity), 15.5f, 1.0f);

    auto ConstructModeCapsule = [&](Rectangle2D Capsule, const char* Label, bool Active, int IconSelector) -> bool
    {
        const bool Hovered = !Reading.OverlayBlockingInput && Capsule.Contains(Cursor);
        if (Active)
        {
            Surface.FillRectangle(Capsule, ScaleAlpha(ColorRgba{ 44, 44, 44, 255 }, Opacity), 12.5f);
            Surface.StrokeRectangle(Capsule, ScaleAlpha(ColorRgba{ 255, 255, 255, 20 }, Opacity), 12.5f, 1.0f);
        }
        else if (Hovered)
        {
            Surface.FillRectangle(Capsule, ScaleAlpha(ColorRgba{ 30, 30, 30, 255 }, Opacity), 12.5f);
        }

        const ColorRgba CapsuleBg = ScaleAlpha(
            Active ? ColorRgba{ 44, 44, 44, 255 }
                   : (Hovered ? ColorRgba{ 30, 30, 30, 255 } : WorkspaceSurfaceColors::ModeStripRecess),
            Opacity);
        const ColorRgba Ink = ScaleAlpha(
            Active ? ColorRgba{ 236, 236, 236, 255 }
                   : (Hovered ? ColorRgba{ 176, 176, 176, 255 } : ColorRgba{ 122, 122, 122, 255 }),
            Opacity);
        const Point2D IconPos{ Capsule.X + 9.0f, Capsule.Y + 6.5f };

        if (IconSelector == 0)
        {
            WorkspaceAppearance::ConstructEditIcon(Surface, IconPos, 12.0f, Ink);
        }
        else if (IconSelector == 1)
        {
            WorkspaceAppearance::ConstructSimulateIcon(Surface, IconPos, 12.0f, Ink, CapsuleBg);
        }
        else if (IconSelector == 2)
        {
            WorkspaceAppearance::ConstructPlayIcon(Surface, IconPos, 12.0f, Ink);
        }
        else
        {
            WorkspaceAppearance::ConstructPauseIcon(
                Surface,
                IconPos,
                12.0f,
                ScaleAlpha(WorkspaceSurfaceColors::SelectionAmber, Opacity));
        }

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { Capsule.X + 25.0f, Capsule.Y + 7.0f },
            10.0f,
            Ink,
            Label);

        return Hovered && Pointer.Pressed;
    };

    const bool ShowingPause = (Reading.ActivePhase == ExecutionStripPhase::Play) && !Reading.PlayPaused && Reading.ShowPauseWhenPlaying;
    const char* PlayLabel   = ShowingPause ? Reading.PauseCaption : Reading.PlayCaption;

    const Rectangle2D EditCapsule{ ModeStrip.X + 3.0f, ModeStrip.Y + 3.0f, EditCapsuleWidth, 25.0f };
    const Rectangle2D SimCapsule{ EditCapsule.Right() + 2.0f, ModeStrip.Y + 3.0f, SimulateCapsuleWidth, 25.0f };
    const Rectangle2D PlayCapsule{ SimCapsule.Right() + 2.0f, ModeStrip.Y + 3.0f, PlayCapsuleWidth, 25.0f };

    if (ConstructModeCapsule(EditCapsule, Reading.EditCaption, Reading.ActivePhase == ExecutionStripPhase::Edit, 0))
    {
        Result.PhaseChanged  = true;
        Result.SelectedPhase = ExecutionStripPhase::Edit;
    }
    if (ConstructModeCapsule(SimCapsule, Reading.SimulateCaption, Reading.ActivePhase == ExecutionStripPhase::Simulate, 1))
    {
        Result.PhaseChanged  = true;
        Result.SelectedPhase = ExecutionStripPhase::Simulate;
    }
    if (ConstructModeCapsule(PlayCapsule, PlayLabel, Reading.ActivePhase == ExecutionStripPhase::Play, ShowingPause ? 3 : 2))
    {
        Result.PhaseChanged        = true;
        Result.SelectedPhase       = ExecutionStripPhase::Play;
        Result.PlayToggleRequested = true;
    }

    return Result;
}

} // namespace Frontier
