//============================================================================================================================================
//                                                     SKETCHOPERATIONDRIVER.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/SketchOperationDriver/Api/SketchOperationDriver.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"
#include "SlateUI/Interface/OptionControls/Api/OptionControls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Slate
{

namespace
{

constexpr double ProjectionPi = 3.14159265358979323846;

/// 🧩 The ray under the pointer, in world space.
/// 📝 The same derivation the picker and the gizmo use. It is repeated rather than shared because the two
///    that have it are both `namespace {}` locals in units this one must not depend on; a third copy is
///    the lesser evil against an upward edge in the unit graph.
bool RayUnderPointer(const ResolvedCamera& Camera,
                     const PlaneExtent& Extent,
                     float ScreenX,
                     float ScreenY,
                     SpatialPoint& RayOrigin,
                     SpatialDirection& RayDirection)
{
    const double CentreX = Extent.MinimumX + Extent.Width() * 0.5;
    const double CentreY = Extent.MinimumY + Extent.Height() * 0.5;
    const double NdcX = (static_cast<double>(ScreenX) - CentreX)
                      / std::max(static_cast<double>(Extent.Width()) * 0.5, 1.0);
    const double NdcY = (CentreY - static_cast<double>(ScreenY))
                      / std::max(static_cast<double>(Extent.Height()) * 0.5, 1.0);

    if (!Camera.Perspective)
    {
        const double Along = NdcX / std::max(Camera.OrthoScale, 0.001) * (Extent.Width() * 0.5);
        const double Upward = NdcY / std::max(Camera.OrthoScale, 0.001) * (Extent.Height() * 0.5);
        RayOrigin = Added(Camera.Frame.Eye,
                          Added(Scaled(Camera.Frame.Right, Along), Scaled(Camera.Frame.Up, Upward)));
        RayDirection = Normalize(Camera.Frame.Forward);
        return true;
    }

    const double TanHalf = std::tan(Camera.FieldOfViewDegrees * 0.5 * ProjectionPi / 180.0);
    const double Aspect = Extent.Width() / std::max(Extent.Height(), 1.0f);
    RayOrigin = Camera.Frame.Eye;
    RayDirection = Normalize(Added(Added(Scaled(Camera.Frame.Right, NdcX * TanHalf * Aspect),
                                         Scaled(Camera.Frame.Up, NdcY * TanHalf)),
                                   Camera.Frame.Forward));
    return true;
}

/// 🧩 Where the pointer meets the active workplane.
/// 🔴 EVERY OPERATION ASKS THIS ONCE AND SHARES THE ANSWER. Two arms deriving the point separately would
///    eventually disagree by a rounding step, and the preview would name a different corner from the
///    commit -- which is a bug nobody could reproduce.
bool PointerOnWorkplane(const ResolvedCamera& Camera,
                        const PlaneExtent& Extent,
                        const PointerCondition& Pointer,
                        const WorldPlacementFrame& Workplane,
                        SpatialPoint& Position)
{
    if (!Workplane.Declared())
        return false;

    SpatialPoint RayOrigin = {};
    SpatialDirection RayDirection = {};
    if (!RayUnderPointer(Camera, Extent, Pointer.PositionX, Pointer.PositionY, RayOrigin, RayDirection))
        return false;

    return ResolveWorldPlacementIntersection(Workplane, RayOrigin, RayDirection, Position);
}

/// 🧩 How many world units one screen pixel spans, at a stated point, under this camera.
/// 🔴 THE WHOLE REASON FILLET AND CHAMFER LOOKED DEAD. A probe reach fixed in world units is hittable at
///    exactly one zoom: at metre scale a twelve-millimetre reach is a fraction of a pixel, so the pointer
///    can never land inside it, no corner is ever resolved, and the tool never leaves `Idle`. Converting a
///    constant PIXEL reach through the camera keeps the target the same size on screen however far in or
///    out the artist is, which is what the curve picker has always done.
/// 📝 Orthographic is exact -- `OrthoScale` IS pixels per world unit. Perspective varies with depth, so it
///    is measured at the probe: the span of one pixel at that distance along the view.
double WorldUnitsPerPixel(const ResolvedCamera& Camera,
                          const PlaneExtent& Extent,
                          const SpatialPoint& At)
{
    if (!Camera.Perspective)
        return 1.0 / std::max(Camera.OrthoScale, 1.0e-6);

    const double Height = std::max(static_cast<double>(Extent.Height()), 1.0);
    const double TanHalf = std::tan(Camera.FieldOfViewDegrees * 0.5 * ProjectionPi / 180.0);

    // 📐 Depth along the view direction, not the straight-line distance: a point off to the side of a
    //    perspective frustum is further from the eye than it is deep, and using the longer figure would
    //    quietly widen the reach toward the edges of the leaf.
    const double Depth = std::abs(Dot(Difference(Camera.Frame.Eye, At),
                                      Normalize(Camera.Frame.Forward)));

    return 2.0 * std::max(Depth, 1.0e-6) * TanHalf / Height;
}

/// 🧩 Which corner manner a tool asks for, and whether it asks for one at all.
bool CornerMannerFor(ParametricToolSubject Subject, CornerManner& Manner)
{
    if (Subject == ParametricToolSubject::Fillet)
    {
        Manner = CornerManner::Fillet;
        return true;
    }
    if (Subject == ParametricToolSubject::Chamfer)
    {
        Manner = CornerManner::Chamfer;
        return true;
    }
    return false;
}

/// 🧩 Which operation manner a tool asks for, and whether it asks for one at all.
bool OperationMannerFor(ParametricToolSubject Subject, OperationManner& Manner)
{
    switch (Subject)
    {
        case ParametricToolSubject::Cut:      Manner = OperationManner::Cut;    return true;
        case ParametricToolSubject::Trim:     Manner = OperationManner::Trim;   return true;
        case ParametricToolSubject::Extend:   Manner = OperationManner::Extend; return true;
        case ParametricToolSubject::Offset:   Manner = OperationManner::Offset; return true;
        case ParametricToolSubject::FillFace: Manner = OperationManner::Fill;   return true;
        default:                                                               return false;
    }
}

/// 🧩 The heading and glyph a readout shows for a tool.
void TitleFor(ParametricToolSubject Subject, const char*& Title, SymbolSubject& Glyph)
{
    switch (Subject)
    {
        case ParametricToolSubject::Fillet:
            Title = "Fillet";
            Glyph = SymbolSubject::FilletRadius;
            break;
        case ParametricToolSubject::Chamfer:
            Title = "Chamfer";
            Glyph = SymbolSubject::BevelChamfer;
            break;
        case ParametricToolSubject::Offset:
            Title = "Offset";
            Glyph = SymbolSubject::FilletRadius;
            break;
        default:
            Title = "Operation";
            Glyph = SymbolSubject::SubjectCount;
            break;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------

void DriveSketchOperations(const PlaneExtent& Bounds,
                           const PointerCondition& Pointer,
                           const ResolvedCamera& Camera,
                           ParametricToolSubject ActiveTool,
                           const WorldPlacementFrame& Workplane,
                           const std::vector<WorldCurveName>& Selection,
                           WorldSketchStructure& World,
                           SketchOperationState& State,
                           ToolContextMenu& Readout,
                           bool& PointerTaken)
{
    // 🔴 CHANGING TOOL ABANDONS WHATEVER WAS IN FLIGHT. A half-dragged fillet left standing when the
    //    artist picks Trim would apply itself the next time anything called Apply -- writing geometry the
    //    artist asked for under a different tool and then walked away from.
    if (ActiveTool != State.Prepared)
    {
        CancelCornerDragSession(State.Corner);
        CancelSketchOperationSession(State.Operation);
        Readout.Close();
        State.Prepared = ActiveTool;
    }

    if (!OperationToolStanding(ActiveTool))
        return;

    SpatialPoint Probe = {};
    const bool Reached = PointerOnWorkplane(Camera, Bounds, Pointer, Workplane, Probe);

    // 📝 The contact only counts when the pointer is actually on the plane. Off it, the operation is
    //    blind, and consuming a press it cannot place would swallow clicks for no reason.
    const bool Pressed  = Reached && Pointer.ContactPressed;
    const bool Held     = Reached && Pointer.ContactHeld;
    const bool Released = Reached && Pointer.ContactReleased;

    CornerManner Manner = CornerManner::Fillet;
    OperationManner Operating = OperationManner::Cut;

    //------------------------------------------------------------------------------------------------------------------------
    // ① Fillet and Chamfer: the corner gesture.
    //------------------------------------------------------------------------------------------------------------------------
    if (CornerMannerFor(ActiveTool, Manner))
    {
        State.Corner.Manner = Manner;

        // 🔴 A REACH THE ARTIST CAN SEE. Converted from a constant pixel target through this frame's
        //    camera, so grabbing a corner takes the same gesture whether the part is ten millimetres
        //    or ten metres across.
        const double Reach = CornerProbeReachPixels * WorldUnitsPerPixel(Camera, Bounds, Probe);

        AdvanceCornerDragSession(World, { Probe, Pressed, Held, Released, Reach }, State.Corner);

        if (State.Corner.PopupStanding() && !Readout.Standing())
            Readout.Open();
        if (!State.Corner.PopupStanding() && Readout.Standing())
            Readout.Close();

        // 🔴 SHOWN IN THE ARTIST'S UNIT. The session's radius is millimetres; the readout is whatever
        //    the panel says. Publishing the raw figure is what put 500 in a box labelled metres.
        if (State.Corner.Phase == CornerPhase::Dragging || State.Corner.Phase == CornerPhase::Pending)
            State.Figure = static_cast<float>(ToDisplay(State.Corner.Radius, State.Unit));

        if (State.Corner.Phase != CornerPhase::Idle)
            PointerTaken = true;
    }
    //------------------------------------------------------------------------------------------------------------------------
    // ② Cut, Trim, Extend, Offset, Fill.
    //------------------------------------------------------------------------------------------------------------------------
    else if (OperationMannerFor(ActiveTool, Operating))
    {
        State.Operation.Manner = Operating;
        State.Chain = Selection;

        // 🔴 THE SAME VIEW-DERIVED REACH THE CORNER GESTURE TAKES. Reaching a curve is the whole
        //    precondition of Cut, Trim, Extend and Fill, so a reach fixed in world units is the whole
        //    reason they did nothing at metre scale.
        const double OperationReach = OperationProbeReachPixels * WorldUnitsPerPixel(Camera, Bounds, Probe);

        AdvanceSketchOperationSession(World, State.Chain, Workplane,
                                      { Probe, Pressed, Held, Released, OperationReach },
                                      State.Operation);

        // 🔴 THE CLICK OPERATIONS COMMIT HERE AND RAISE NOTHING. Only Offset carries a figure, so only
        //    Offset reaches the readout at all -- which is why the popup is asked about the session's
        //    own answer rather than about which tool is active.
        if (State.Operation.Phase == OperationPhase::Applied)
        {
            std::vector<WorldCurveName> Produced;
            static_cast<void>(PerformSketchOperation(World, State.Chain, Workplane,
                                                     State.Operation, Produced));
        }

        if (State.Operation.ReadoutStanding() && !Readout.Standing())
            Readout.Open();
        if (!State.Operation.ReadoutStanding() && Readout.Standing())
            Readout.Close();

        if (State.Operation.Phase == OperationPhase::Dragging ||
            State.Operation.Phase == OperationPhase::Pending)
            State.Figure = static_cast<float>(ToDisplay(State.Operation.Distance, State.Unit));

        if (State.Operation.Phase != OperationPhase::Idle)
            PointerTaken = true;
    }

    //------------------------------------------------------------------------------------------------------------------------
    // ③ The readout, for the two operations that carry a figure.
    //------------------------------------------------------------------------------------------------------------------------
    if (!Readout.Standing())
        return;

    const char* Title = "";
    SymbolSubject Glyph = SymbolSubject::SubjectCount;
    TitleFor(ActiveTool, Title, Glyph);

    // 🔴 THE FIGURE MOVES INTO THE HEADING, because the row that used to state it has gone. A readout
    //    that named the operation but not its value would leave the artist dragging blind -- the very
    //    complaint the preview is being added to answer.
    // 📝 Written into the state's own buffer rather than a local, because `PopupDeclaration::Title` is
    //    BORROWED and read after this function returns; a stack buffer here would dangle.
    std::snprintf(State.Heading, sizeof(State.Heading), "%s  %.*f %s",
                  Title,
                  static_cast<int>(MeasureUnitPlaces(State.Unit)),
                  static_cast<double>(State.Figure),
                  MeasureUnitSuffix(State.Unit));

    // 🔴 NO SLIDER. The artist asked for it gone, and the gesture is the reason it can go: the DRAG sets
    //    the figure, so a track in the readout was a second way to do the thing the pointer was already
    //    doing -- and the one that had to be dragged with the other hand while the first held the corner.
    //    The readout states the figure and offers Apply and Cancel; the pointer sets it.
    // 📝 The clamp still governs. It is applied to the drag and to any typed figure in the session
    //    itself, so removing the track removes a control, not a rule.
    OptionDeclaration Rows[1] = {};
    const std::uint32_t RowCount = 0u;

    PopupDeclaration Declared = {};
    Declared.Title    = State.Heading;
    Declared.Glyph    = Glyph;
    Declared.Rows     = Rows;
    Declared.RowCount = RowCount;

    bool ReadoutTaken = false;
    const Deliver<PopupVerdict> Verdict = Readout.Record(Bounds, Declared, ReadoutTaken);
    if (ReadoutTaken)
        PointerTaken = true;
    if (!Verdict.Resolved)
        return;

    // 📝 The figure the artist may have typed goes back through the session's clamp before it is used,
    //    so the drag and the readout cannot disagree about what the largest legal value is.
    // 🔴 CONVERTED ON THE WAY IN, ONCE, exactly as the annotation band does it. Type 0.05 with metres
    //    showing and the session is asked for 50 millimetres; the sessions never learn metres exist.
    const double TypedMillimetres = ToMillimetres(static_cast<double>(State.Figure), State.Unit);

    if (CornerMannerFor(ActiveTool, Manner))
    {
        DeclareCornerRadius(World, State.Corner, TypedMillimetres);
        if (Verdict.Delivered == PopupVerdict::Applied)
        {
            WorldCurveName Produced = {};
            static_cast<void>(ApplyCornerDragSession(World, State.Corner, Produced));
        }
        else if (Verdict.Delivered == PopupVerdict::Cancelled)
        {
            CancelCornerDragSession(State.Corner);
        }
    }
    else
    {
        DeclareOperationDistance(State.Operation, TypedMillimetres);
        if (Verdict.Delivered == PopupVerdict::Applied)
        {
            std::vector<WorldCurveName> Produced;
            static_cast<void>(PerformSketchOperation(World, State.Chain, Workplane,
                                                     State.Operation, Produced));
        }
        else if (Verdict.Delivered == PopupVerdict::Cancelled)
        {
            CancelSketchOperationSession(State.Operation);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IT SHOWS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

/// 🧩 Draws one world-space segment into the packet, if both ends are on screen.
/// 📝 Skipped rather than clipped when an end falls behind the eye. A preview is a hint; a hint that has
///    to be clipped correctly is a renderer, and this is not one.
bool AppendPreviewSegment(const ResolvedCamera& Camera,
                          const PlaneExtent& Extent,
                          const SpatialPoint& From,
                          const SpatialPoint& To,
                          Unsigned32 Colour,
                          Real32 Thickness,
                          WorkspaceCadPacket& Delivered)
{
    float FromX = 0.0f;
    float FromY = 0.0f;
    float ToX   = 0.0f;
    float ToY   = 0.0f;
    if (!ProjectFromCamera(Camera, Extent, From, FromX, FromY) ||
        !ProjectFromCamera(Camera, Extent, To, ToX, ToY))
        return false;

    Delivered.AddSegment(FromX, FromY, ToX, ToY, Colour, Thickness);
    return true;
}

} // namespace

Deliver<bool> ProjectOperationPreview(const SketchOperationState& State,
                                      ParametricToolSubject ActiveTool,
                                      const ResolvedCamera& Camera,
                                      const PlaneExtent& PhysicalExtent,
                                      WorkspaceCadPacket& Delivered,
                                      const OperationPreviewStyle& Style)
{
    if (!OperationToolStanding(ActiveTool))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the active tool is not one of the seven operations" });

    bool Appended = false;

    //--------------------------------------------------------------------------------------------------------------------
    // ① Fillet and Chamfer: the corner that would replace the sharp one.
    //--------------------------------------------------------------------------------------------------------------------
    CornerManner Manner = CornerManner::Fillet;
    if (CornerMannerFor(ActiveTool, Manner))
    {
        const CornerDragSession& Corner = State.Corner;

        // 📝 The corner itself, marked as soon as it is under the pointer. This alone answers "is the
        //    tool seeing anything?", which is the question the artist asks first.
        if (Corner.Phase != CornerPhase::Idle && Corner.Target.Declared())
        {
            float MarkX = 0.0f;
            float MarkY = 0.0f;
            if (ProjectFromCamera(Camera, PhysicalExtent, Corner.Target.Position, MarkX, MarkY))
            {
                Delivered.AddMarker(MarkX, MarkY, Style.MarkerColour, Style.MarkerRadius,
                                    WorkspaceCadMarkerSubject::SketchControl);
                Appended = true;
            }
        }

        // 🔴 THE SHAPE THE RADIUS WOULD PRODUCE. Tessellated through the SAME three-point arc the commit
        //    declares, so what is drawn is what gets written rather than a circle that resembles it.
        if (Corner.Shaped)
        {
            const CurveSpecification Preview =
                Corner.Manner == CornerManner::Chamfer
                    ? CurveSpecification::DeclareLine(Corner.EnterPoint, Corner.ExitPoint)
                    : CurveSpecification::DeclareThreePointArc(Corner.EnterPoint, Corner.Through,
                                                               Corner.ExitPoint);

            if (Preview.Declared())
            {
                std::vector<SpatialPoint> Polyline;
                AppendCurvePolyline(Preview, Polyline, Style.ArcSteps);
                for (std::size_t Index = 0u; Index + 1u < Polyline.size(); ++Index)
                    Appended = AppendPreviewSegment(Camera, PhysicalExtent,
                                                    Polyline[Index], Polyline[Index + 1u],
                                                    Style.AddingColour, Style.Thickness, Delivered)
                             || Appended;
            }

            // 📝 The two tangent points, so the artist can see how much of each leg is being eaten.
            for (const SpatialPoint& Tangent : { Corner.EnterPoint, Corner.ExitPoint })
            {
                float TangentX = 0.0f;
                float TangentY = 0.0f;
                if (ProjectFromCamera(Camera, PhysicalExtent, Tangent, TangentX, TangentY))
                {
                    Delivered.AddMarker(TangentX, TangentY, Style.AddingColour,
                                        Style.MarkerRadius * 0.7f,
                                        WorkspaceCadMarkerSubject::SketchControl);
                    Appended = true;
                }
            }
        }

        return Appended
             ? Deliver<bool>::Result(true)
             : Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "no corner is under the pointer, so there is nothing to preview" });
    }

    //--------------------------------------------------------------------------------------------------------------------
    // ② Trim and Cut: what would be removed, and where the division would fall.
    //--------------------------------------------------------------------------------------------------------------------
    const SketchOperationSession& Operating = State.Operation;
    if (Operating.Preview != OperationVerdict::Produced)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the operation would refuse here, so nothing is promised" });

    if (Operating.Manner == OperationManner::Trim)
    {
        // 🔴 IN RED, BECAUSE IT IS ABOUT TO BE DELETED. Trim resolves its own bounds, so without this the
        //    artist cannot tell which of several segments a click will take -- and finding out by
        //    clicking is destructive.
        Appended = AppendPreviewSegment(Camera, PhysicalExtent,
                                        Operating.DepartingFrom, Operating.DepartingTo,
                                        Style.RemovingColour, Style.Thickness * 1.6f, Delivered)
                 || Appended;

        for (const SpatialPoint& End : { Operating.DepartingFrom, Operating.DepartingTo })
        {
            float EndX = 0.0f;
            float EndY = 0.0f;
            if (ProjectFromCamera(Camera, PhysicalExtent, End, EndX, EndY))
            {
                Delivered.AddMarker(EndX, EndY, Style.RemovingColour, Style.MarkerRadius * 0.7f,
                                    WorkspaceCadMarkerSubject::SketchControl);
                Appended = true;
            }
        }
    }
    else if (Operating.Manner == OperationManner::Cut)
    {
        // 📝 One marker, on the curve, exactly where the two pieces will meet.
        float CutX = 0.0f;
        float CutY = 0.0f;
        if (ProjectFromCamera(Camera, PhysicalExtent, Operating.Division, CutX, CutY))
        {
            Delivered.AddMarker(CutX, CutY, Style.MarkerColour, Style.MarkerRadius,
                                WorkspaceCadMarkerSubject::SketchControl);
            Appended = true;
        }
    }
    else if (Operating.Manner == OperationManner::Extend)
    {
        // 📝 Extend already knew where it would land; it simply had nowhere to say so.
        float LandX = 0.0f;
        float LandY = 0.0f;
        if (ProjectFromCamera(Camera, PhysicalExtent, Operating.Landing, LandX, LandY))
        {
            Delivered.AddMarker(LandX, LandY, Style.AddingColour, Style.MarkerRadius,
                                WorkspaceCadMarkerSubject::SketchControl);
            Appended = true;
        }
    }

    return Appended
         ? Deliver<bool>::Result(true)
         : Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                   "this manner draws no preview" });
}

} // namespace Slate
