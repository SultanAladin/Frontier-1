//============================================================================================================================================
//                                                        ANNOTATIONDRIVER.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/AnnotationDriver/Api/AnnotationDriver.h"

#include "SlateUI/Interface/OptionControls/Api/OptionControls.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{

constexpr double ProjectionPi = 3.14159265358979323846;

/// 🧩 The ray under the pointer, in world space.
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

/// 🧩 The heading and glyph a readout shows for an annotation.
const char* TitleFor(WorldDimensionSubject Subject)
{
    switch (Subject)
    {
        case WorldDimensionSubject::Radius:     return "Radius";
        case WorldDimensionSubject::Diameter:   return "Diameter";
        case WorldDimensionSubject::Angle:      return "Angle";
        case WorldDimensionSubject::Horizontal: return "Horizontal";
        case WorldDimensionSubject::Vertical:   return "Vertical";
        case WorldDimensionSubject::Aligned:
        case WorldDimensionSubject::SubjectCount:
        default:                                return "Dimension";
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------

void DriveAnnotations(const PlaneExtent& Bounds,
                      const PointerCondition& Pointer,
                      const ResolvedCamera& Camera,
                      ParametricToolSubject ActiveTool,
                      const WorldPlacementFrame& Workplane,
                      const WorldPick& Hovered,
                      WorldDimensionName FigureUnderPointer,
                      WorldSketchStructure& World,
                      AnnotationState& State,
                      ToolContextMenu& Readout,
                      bool& PointerTaken)
{
    State.Refused = false;

    // 📝 The withdrawal notice ages on every frame, whatever tool is held, so it expires on its own
    //    rather than lingering until the next annotation happens to be applied.
    if (State.NoticeFramesLeft > 0u)
        --State.NoticeFramesLeft;

    // 🔴 CHANGING TOOL ABANDONS WHATEVER WAS IN FLIGHT, exactly as it does for the operations. A
    //    half-placed dimension left standing would attach itself to the next tool's picks.
    if (ActiveTool != State.Prepared)
    {
        CancelAnnotationSession(World, State.Session);
        Readout.Close();
        State.Prepared = ActiveTool;
    }

    const AnnotationIntent Intent = ResolveAnnotationIntent(ActiveTool);
    if (!Intent.Standing)
        return;

    State.Session.Constraining = Intent.Constraining;
    State.Session.Dimension = Intent.Dimension;
    State.Session.Constraint = Intent.Constraint;

    // 📝 An unsupported tile owns the pointer so nothing else grabs it, and does nothing else at all.
    if (!Intent.Supported)
        return;

    //------------------------------------------------------------------------------------------------------------------------
    // ① Picking.
    //------------------------------------------------------------------------------------------------------------------------
    // 🔴 AN EXISTING DIMENSION IS GRASPED BEFORE A NEW ONE IS STARTED. The figure chip sits over the
    //    geometry it measures, so a press that lands on it would otherwise pick the curve underneath and
    //    declare a second dimension on top of the first -- and the first could never be moved again.
    if (Pointer.ContactPressed && FigureUnderPointer.Assigned() &&
        State.Session.Phase != AnnotationPhase::Placing &&
        GraspDeclaredDimension(World, FigureUnderPointer, State.Session))
    {
        State.Figure = static_cast<float>(ToDisplay(State.Session.Figure, State.Unit));
        PointerTaken = true;
    }
    else if (Pointer.ContactPressed && Hovered.Standing() &&
        State.Session.Phase != AnnotationPhase::Placing &&
        State.Session.Phase != AnnotationPhase::Editing)
    {
        const AnnotationVerdict Offered = OfferAnnotationPick(World, Hovered, State.Session);
        PointerTaken = true;

        if (Offered == AnnotationVerdict::Produced && State.Session.Constraining)
        {
            // 🔴 A CONSTRAINT COMMITS ON ITS LAST PICK. It has no figure, so a readout asking Apply would
            //    be asking the artist to confirm something they have already fully specified.
            static_cast<void>(ApplyAnnotation(World, State.Session));
            CancelAnnotationSession(World, State.Session);
            return;
        }

        if (Offered == AnnotationVerdict::Produced)
            State.Figure = static_cast<float>(ToDisplay(State.Session.Figure, State.Unit));
    }

    if (State.Session.Constraining)
        return;

    //------------------------------------------------------------------------------------------------------------------------
    // ② Dragging the placed dimension off to the side.
    //------------------------------------------------------------------------------------------------------------------------
    if (State.Session.Phase == AnnotationPhase::Placing)
    {
        SpatialPoint RayOrigin = {};
        SpatialDirection RayDirection = {};
        SpatialPoint Probe = {};
        if (Workplane.Declared() &&
            RayUnderPointer(Camera, Bounds, Pointer.PositionX, Pointer.PositionY,
                            RayOrigin, RayDirection) &&
            ResolveWorldPlacementIntersection(Workplane, RayOrigin, RayDirection, Probe))
        {
            DragAnnotationTo(World, Probe, State.Session);
        }

        PointerTaken = true;

        // 📝 The release settles the placement and opens the figure for typing. It does not commit, so
        //    the artist can still walk away without the drawing having moved.
        if (Pointer.ContactReleased)
            State.Session.Phase = AnnotationPhase::Editing;
    }

    //------------------------------------------------------------------------------------------------------------------------
    // ③ The readout.
    //------------------------------------------------------------------------------------------------------------------------
    if (State.Session.ReadoutStanding() && !Readout.Standing())
        Readout.Open();
    if (!State.Session.ReadoutStanding() && Readout.Standing())
        Readout.Close();
    if (!Readout.Standing())
        return;

    OptionDeclaration Rows[1] = {};
    Rows[0].Kind    = OptionControl::Slider;
    Rows[0].Caption = "Value";
    Rows[0].Unit    = MeasureUnitSuffix(State.Unit);
    Rows[0].Reading = &State.Figure;
    Rows[0].Places  = MeasureUnitPlaces(State.Unit);
    Rows[0].Minimum = 0.0f;

    // 📝 A dimension has no natural upper bound, so the range simply follows the value: it is an entry
    //    field with a slider attached, not a constrained one like the fillet's radius.
    Rows[0].Maximum = std::max(State.Figure * 4.0f, 1.0f);

    PopupDeclaration Declared = {};
    Declared.Title    = TitleFor(Intent.Dimension);
    Declared.Glyph    = SymbolSubject::ConstraintDimension;
    Declared.Rows     = Rows;
    Declared.RowCount = 1u;

    bool ReadoutTaken = false;
    const Deliver<PopupVerdict> Verdict = Readout.Record(Bounds, Declared, ReadoutTaken);
    if (ReadoutTaken)
        PointerTaken = true;
    if (!Verdict.Resolved)
        return;

    if (Verdict.Delivered == PopupVerdict::Applied)
    {
        // 🔴 THE TYPED FIGURE IS CONVERTED ON THE WAY IN, ONCE. Type 4.2 with metres showing and 4200
        //    millimetres is what the sketch is asked for -- the model never learns that metres exist.
        const double Millimetres = ToMillimetres(static_cast<double>(State.Figure), State.Unit);
        if (DeclareAnnotationFigure(State.Session, Millimetres) == AnnotationVerdict::Produced)
        {
            // 🔴 A SOLVER REFUSAL LEAVES THE DRAWING ALONE and is reported, rather than being swallowed.
            //    This is the entire reason typed edits go through the solver instead of straight into the
            //    parameters: the sketch is allowed to say no.
            const AnnotationVerdict Applied = ApplyAnnotation(World, State.Session);
            State.Refused = Applied == AnnotationVerdict::SolverRefused;

            // 🔴 WHAT THE EDIT COST IS CARRIED OUT OF THE SESSION BEFORE IT IS CLEARED. The dimension
            //    outranks the constraints, so a typed value can withdraw a relation the artist set up
            //    earlier -- and they are told, because a modeller that quietly dissolves the artist's
            //    own rules is one they will stop trusting with work they care about.
            State.RetiredCount = Applied == AnnotationVerdict::ProducedByRetiringConstraints
                                     ? static_cast<std::uint32_t>(State.Session.RetiredConstraints.size())
                                     : 0u;

            // 📝 About four seconds at sixty frames -- long enough to read, short enough that it is
            //    gone before it becomes part of the furniture.
            State.NoticeFramesLeft = State.RetiredCount > 0u ? 240u : 0u;
        }
        if (!State.Refused)
            CancelAnnotationSession(World, State.Session);
    }
    else if (Verdict.Delivered == PopupVerdict::Cancelled)
    {
        CancelAnnotationSession(World, State.Session);
    }
}

} // namespace Slate
