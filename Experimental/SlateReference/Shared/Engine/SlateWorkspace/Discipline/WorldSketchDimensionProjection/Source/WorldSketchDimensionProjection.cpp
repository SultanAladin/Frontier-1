//============================================================================================================================================
//                                               WORLDSKETCHDIMENSIONPROJECTION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/WorldSketchDimensionProjection/Api/WorldSketchDimensionProjection.h"

#include "Shared/WorkspaceCadNearClip.slang.h"
#include "SlateWorkspace/Discipline/AnnotationSession/Api/AnnotationSession.h"

#include <cmath>
#include <cstring>

namespace Slate
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WORLD TO SCREEN
//------------------------------------------------------------------------------------------------------------------------

// 📝 The same projection the curve renderer uses, and deliberately the same arithmetic: a dimension drawn
//    through a different path would drift off its own geometry by a pixel at some zoom levels, which is
//    exactly the staleness the whole design exists to prevent.
WorkspaceCadProjectedPoint ProjectPoint(const ResolvedCamera& Camera,
                                        const PlaneExtent& Extent,
                                        const SpatialPoint& Position)
{
    WorkspaceCadProjectedPoint Point = {};
    if (!Camera.Perspective)
    {
        float ScreenX = 0.0f;
        float ScreenY = 0.0f;
        static_cast<void>(ProjectFromCamera(Camera, Extent, Position, ScreenX, ScreenY));
        Point.X = ScreenX;
        Point.Y = ScreenY;
        Point.W = 1.0f;
        return Point;
    }

    const SpatialDirection EyeToPoint = Difference(Camera.Frame.Eye, Position);
    const double CameraX = Dot(EyeToPoint, Camera.Frame.Right);
    const double CameraY = Dot(EyeToPoint, Camera.Frame.Up);
    const double CameraZ = Dot(EyeToPoint, Camera.Frame.Forward);
    const double TanHalf = std::tan(Camera.FieldOfViewDegrees * 0.5 * ProjectionPi / 180.0);
    const double Focal = (Extent.Height() * 0.5) / (TanHalf > 1.0e-6 ? TanHalf : 1.0e-6);
    const double CentreX = Extent.MinimumX + Extent.Width() * 0.5;
    const double CentreY = Extent.MinimumY + Extent.Height() * 0.5;

    Point.X = static_cast<Real32>(CentreX * CameraZ + Focal * CameraX);
    Point.Y = static_cast<Real32>(CentreY * CameraZ - Focal * CameraY);
    Point.W = static_cast<Real32>(CameraZ);
    return Point;
}

void AppendStroke(const ResolvedCamera& Camera,
                  const PlaneExtent& Extent,
                  const SpatialPoint& Start,
                  const SpatialPoint& End,
                  Unsigned32 Packed,
                  Real32 Thickness,
                  WorkspaceCadPacket& Delivered)
{
    WorkspaceCadProjectedPoint First = ProjectPoint(Camera, Extent, Start);
    WorkspaceCadProjectedPoint Second = ProjectPoint(Camera, Extent, End);

    // 🔴 A dimension behind the eye must not be drawn wrapping across the viewport. The near clip is the
    //    same one the curves go through, so annotation disappears exactly when its geometry does.
    if (Camera.Perspective && !ClipWorkspaceCadSegmentNear(First, Second))
        return;

    const WorkspaceCadScreenPoint A = ResolveWorkspaceCadScreenPoint(First);
    const WorkspaceCadScreenPoint B = ResolveWorkspaceCadScreenPoint(Second);
    Delivered.AddSegment(A.X, A.Y, B.X, B.Y, Packed, Thickness);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ARROWHEADS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Draws one arrowhead as a pair of barbs, in world millimetres.
/// note  📝 Built from the arrow's own direction and the dimension's plane normal, so it lies flat in the
///        drawing plane whatever way the camera is looking. Deriving the barbs from screen space instead
///        would make them swim as the view orbited.
void AppendArrowhead(const ResolvedCamera& Camera,
                     const PlaneExtent& Extent,
                     const SpatialPoint& Tip,
                     const SpatialDirection& Facing,
                     const SpatialDirection& Normal,
                     Real32 Spread,
                     Unsigned32 Packed,
                     Real32 Thickness,
                     WorkspaceCadPacket& Delivered)
{
    const SpatialDirection Backward = Normalize(Negated(Facing));
    if (LengthSquared(Backward) <= 0.0)
        return;

    const SpatialDirection Across = Normalize(Cross(Normal, Backward));
    const double Reach = DimensionArrowReach;
    const double Half = Reach * static_cast<double>(Spread);

    const SpatialPoint Base = Added(Tip, Scaled(Backward, Reach));
    const SpatialPoint Left = Added(Base, Scaled(Across, Half));
    const SpatialPoint Right = Added(Base, Scaled(Across, -Half));

    AppendStroke(Camera, Extent, Tip, Left, Packed, Thickness, Delivered);
    AppendStroke(Camera, Extent, Tip, Right, Packed, Thickness, Delivered);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ProjectWorldSketchDimensions(const WorldSketchStructure& Declared,
                                           const ResolvedCamera& Camera,
                                           const PlaneExtent& PhysicalExtent,
                                           MeasureUnit Unit,
                                           WorkspaceCadPacket& Delivered,
                                           std::vector<DimensionFigureChip>& Figures,
                                           WorldDimensionName Selected,
                                           const WorldDimensionRenderingStyle& Style)
{
    Figures.clear();

    for (std::uint32_t Index = 1u; Index <= Declared.DimensionCount(); ++Index)
    {
        const WorldDimensionName Subject = { Index };

        // 🔴 RE-DERIVED, NEVER REMEMBERED. This is the frame-by-frame call that makes a dimension track
        //    the geometry it measures. Caching the result across frames would reintroduce, at this
        //    layer, precisely the staleness the geometry layer was written to make impossible.
        const Deliver<DimensionGeometry> Drawn = ResolveDimensionGeometry(Declared, Subject);

        // 🔴 GONE MEANS NOT DRAWN. A dimension whose edge was deleted has nothing to measure; drawing it
        //    at the origin would put a figure in the middle of the model reporting a length that is not
        //    there. Skipping is the honest answer.
        if (!Drawn.Resolved)
            continue;

        const DimensionGeometry& Geometry = Drawn.Delivered;
        const bool IsSelected = Selected.Assigned() && Selected.IssuedIndex == Index;
        const Unsigned32 LineColour = IsSelected ? Style.SelectedLineColour : Style.LineColour;

        // ── The witness lines ───────────────────────────────────────────────────────────────────────
        // 📝 Only the linear kinds carry them: a radius leader starts at the centre and a diameter spans
        //    the circle, and neither has anything to witness back to.
        if (Geometry.Drawing == DimensionDrawing::Linear)
        {
            const SpatialDirection StartReach = Difference(Geometry.MeasuredStart, Geometry.LineStart);
            const SpatialDirection EndReach = Difference(Geometry.MeasuredEnd, Geometry.LineEnd);
            const SpatialDirection StartOut = Normalize(StartReach);
            const SpatialDirection EndOut = Normalize(EndReach);

            AppendStroke(Camera, PhysicalExtent, Geometry.MeasuredStart,
                         Added(Geometry.LineStart, Scaled(StartOut, DimensionWitnessOvershoot)),
                         Style.WitnessColour, Style.WitnessThickness, Delivered);
            AppendStroke(Camera, PhysicalExtent, Geometry.MeasuredEnd,
                         Added(Geometry.LineEnd, Scaled(EndOut, DimensionWitnessOvershoot)),
                         Style.WitnessColour, Style.WitnessThickness, Delivered);
        }

        // ── The dimension line itself ───────────────────────────────────────────────────────────────
        AppendStroke(Camera, PhysicalExtent, Geometry.LineStart, Geometry.LineEnd,
                     LineColour, Style.LineThickness, Delivered);

        // ── The arrowheads ──────────────────────────────────────────────────────────────────────────
        // 📝 `ArrowStart`/`ArrowEnd` already face the right way, including the outward flip a span too
        //    short to hold its own arrows needs. Nothing is decided again here.
        AppendArrowhead(Camera, PhysicalExtent, Geometry.LineStart, Geometry.ArrowStart,
                        Geometry.Frame.Normal, Style.ArrowSpread, LineColour,
                        Style.LineThickness, Delivered);
        AppendArrowhead(Camera, PhysicalExtent, Geometry.LineEnd, Geometry.ArrowEnd,
                        Geometry.Frame.Normal, Style.ArrowSpread, LineColour,
                        Style.LineThickness, Delivered);

        // ── The figure ──────────────────────────────────────────────────────────────────────────────
        DimensionFigureChip Chip = {};
        Chip.Subject = Subject;
        Chip.Selected = IsSelected;
        ComposeDimensionLabel(Declared, Subject, Unit, true, Chip.Figure, DimensionFigureLimit);

        const WorkspaceCadProjectedPoint At = ProjectPoint(Camera, PhysicalExtent, Geometry.TextAt);
        if (Camera.Perspective && At.W <= 0.0f)
            continue;                                   // [-] - the figure is behind the eye

        const WorkspaceCadScreenPoint Anchor = ResolveWorkspaceCadScreenPoint(At);
        const Real32 Wide = static_cast<Real32>(std::strlen(Chip.Figure)) * Style.FigureCharacterWidth;
        const Real32 HalfWide = Wide * 0.5f + Style.ChipPaddingX;
        const Real32 HalfHigh = Style.FigureHeight * 0.5f + Style.ChipPaddingY;

        Chip.Body = PlaneExtent{ Anchor.X - HalfWide, Anchor.Y - HalfHigh,
                                 Anchor.X + HalfWide, Anchor.Y + HalfHigh };
        Chip.TextX = Anchor.X - Wide * 0.5f;
        Chip.TextY = Anchor.Y - Style.FigureHeight * 0.5f;

        Figures.push_back(Chip);
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------

WorldDimensionName ResolveDimensionFigureAt(const std::vector<DimensionFigureChip>& Figures,
                                            double PositionX,
                                            double PositionY)
{
    // 📝 Backwards, so the chip drawn last -- the one on top where they overlap -- is the one hit.
    for (std::size_t Index = Figures.size(); Index-- > 0u;)
    {
        const DimensionFigureChip& Chip = Figures[Index];
        if (Chip.Body.Encloses(static_cast<float>(PositionX), static_cast<float>(PositionY)))
            return Chip.Subject;
    }
    return {};
}

} // namespace Slate
