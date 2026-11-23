//============================================================================================================================================
//                                                    WORLDSKETCHINTERACTION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/WorldSketchInteraction/Api/WorldSketchInteraction.h"

#include "SlateWorkspace/Discipline/TransformSequence/Api/TransformSequence.h"
#include "SlateWorkspace/Discipline/WorldSketchPicking/Api/WorldSketchScreenPicking.h"

#include <cmath>

namespace Slate
{

namespace
{

GizmoHandle ResolveUniversalGizmoHandle(const GizmoScreenBasis& Screen,
                                        TransformManner Manner,
                                        float PointerX,
                                        float PointerY)
{
    return ResolveGizmoHandle(Screen, Manner, PointerX, PointerY);
}

} // namespace

bool RefreshWorldSketchPick(const WorldSketchStructure& Declared,
                           WorldPick& Pick)
{
    if (!Pick.Standing())
        return false;

    switch (Pick.Subject)
    {
        case WorldPickSubject::Point:
            return ResolveWorldSketchPointPosition(Declared, Pick.Point, Pick.Position);

        case WorldPickSubject::Control:
        {
            std::vector<WorldControlPlacement> Controls;
            if (!Pick.Curve.Assigned() || !ResolveWorldSketchControls(Declared, Pick.Curve, Controls))
                return false;
            for (const WorldControlPlacement& Control : Controls)
                if (Control.Name.IssuedIndex == Pick.Control.IssuedIndex)
                {
                    Pick.Position = Control.Position;
                    return true;
                }
            return false;
        }

        // 🔴 A CURVE AND A LOOP KEPT A STALE PIVOT. Both arms reported the pick still valid and left
        //    `Position` holding wherever the geometry stood when it was FIRST picked, so after a move the
        //    gizmo, the centroid of a multiple selection and the slide anchor all addressed the old place.
        //    A refresh that refuses to re-read the position is not a refresh.
        case WorldPickSubject::Curve:
            return ResolveWorldCurvePivot(Declared, Pick.Curve, Pick.Position);

        case WorldPickSubject::Loop:
            return ResolveWorldLoopPivot(Declared, Pick.Loop, Pick.Position);

        case WorldPickSubject::None:
            return false;
    }

    return false;
}

bool ResolveCurvePolyline(const WorldSketchStructure& Declared,
                          WorldCurveName Curve,
                          std::vector<SpatialPoint>& Polyline)
{
    const DeclaredWorldCurve* Held = Declared.Resolve(Curve);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;

    Polyline.clear();
    AppendCurvePolyline(Held->Geometry, Polyline, 48u);
    return !Polyline.empty();
}

void RefreshWorldSelectionSet(const WorldSketchStructure& Declared, WorldSelectionSet& Set)
{
    for (auto It = Set.Items.begin(); It != Set.Items.end(); )
    {
        if (!RefreshWorldSketchPick(Declared, *It))
            It = Set.Items.erase(It);
        else
            ++It;
    }
}

namespace
{

/// 🧩 The element mode a pick of this kind belongs to.
SelectionElement ElementOfWorldPick(WorldPickSubject Subject)
{
    switch (Subject)
    {
        case WorldPickSubject::Point:
        case WorldPickSubject::Control:
            return SelectionElement::Vertex;

        case WorldPickSubject::Curve:
            return SelectionElement::Edge;

        case WorldPickSubject::Loop:
            return SelectionElement::Face;

        case WorldPickSubject::None:
            break;
    }
    return SelectionElement::Free;
}

/// 🧩 Whether the standing mode still admits a pick that is already selected.
/// note  🔴 THE MODE MUST JUDGE A SELECTION, NOT ONLY MAKE ONE. The picker consults the mode while it
///        decides what the pointer is over, so it governs picks born under the pointer and nothing else.
///        A selection reaches this set by other roads too — seeded from the outliner row through the
///        compatibility bridge, or simply left standing when the artist changes mode — and none of them
///        asked. That is how a vertex came to be selected in Edge mode: the gizmo drew for it, and the
///        drag then refused it, because selecting and transforming answered to different authorities.
/// note  📝 `Object` admits whatever a whole-shape pick resolves to, which is a curve or a loop; it is a
///        statement about what is RETURNED, not a fourth kind of element.
bool SelectionModeAdmits(const SelectionOptions& Selection, const WorldPick& Pick)
{
    if (!Pick.Standing())
        return false;
    if (Selection.Element == SelectionElement::Free)
        return true;
    if (Selection.Element == SelectionElement::Object)
        return Pick.Subject == WorldPickSubject::Curve || Pick.Subject == WorldPickSubject::Loop;
    return ElementOfWorldPick(Pick.Subject) == Selection.Element;
}

/// 🧩 Drops every selected pick the standing mode no longer admits.
void RetainAdmissibleSelection(const SelectionOptions& Selection, WorldSelectionSet& Set)
{
    for (auto It = Set.Items.begin(); It != Set.Items.end(); )
    {
        if (!SelectionModeAdmits(Selection, *It))
            It = Set.Items.erase(It);
        else
            ++It;
    }
}

}   // namespace

void DriveWorldSketchSelectionAndTransform(const PlaneExtent& Extent,
                                          const PointerCondition& Pointer,
                                          const TextInputCondition& TextInput,
                                          const ModifierCondition& Modifiers,
                                          const SelectionOptions& Selection,
                                          const GizmoOptions& Gizmo,
                                          const ResolvedCamera& Camera,
                                          WorldSketchStructure& Declared,
                                          WorldSelectionSet& SelectionSet,
                                          WorldPick& SemanticSelection,
                                          WorldPick& HoveredSelection,
                                          WorldSketchTransformSession& Transform,
                                          bool& PointerTaken,
                                          double SessionMilliseconds,
                                          double& LastGPressedMilliseconds,
                                          GizmoHandle* HoveredHandle)
{
    HoveredSelection = {};
    if (HoveredHandle != nullptr)
        *HoveredHandle = GizmoHandle::None;

    if (Extent.Encloses(Pointer.PositionX, Pointer.PositionY))
        ResolveWorldSketchPickForElement(Declared, Camera, Extent,
                                        Pointer.PositionX, Pointer.PositionY,
                                        Selection.ResolvedTolerance(),
                                        Selection.Element, HoveredSelection);

    // 🔴 THE SEED IS FILTERED TOO. This arm adopts whatever the host handed down — the outliner row,
    //    mirrored through the compatibility bridge — and that road never passed the picker, so an
    //    unadmitted kind entered the set here and drew a gizmo the drag would refuse.
    if (SemanticSelection.Standing() && SelectionSet.Empty() &&
        SelectionModeAdmits(Selection, SemanticSelection))
        SetWorldPick(SelectionSet, SemanticSelection, false);

    RefreshWorldSelectionSet(Declared, SelectionSet);

    // 📝 Applied every frame rather than only when the mode changes, because the mode is owned by the
    //    host widget and this layer is never told that it moved.
    if (!Transform.Engaged())
        RetainAdmissibleSelection(Selection, SelectionSet);

    const WorldPick* Active = SelectionSet.Active();
    WorldPick ActiveSelection = Active != nullptr ? *Active : WorldPick{};
    SemanticSelection = ActiveSelection;

    if (SelectionSet.Items.size() > 1u)
    {
        SpatialPoint Centroid = {};
        for (const WorldPick& Item : SelectionSet.Items)
        {
            Centroid.Left += Item.Position.Left;
            Centroid.Up += Item.Position.Up;
            Centroid.Forward += Item.Position.Forward;
        }
        const double Count = static_cast<double>(SelectionSet.Items.size());
        Centroid.Left /= Count;
        Centroid.Up /= Count;
        Centroid.Forward /= Count;
        ActiveSelection.Position = Centroid;
    }

    GizmoHandle ResolvedHandle = GizmoHandle::None;
    const TransformManner ActiveGizmoManner = Transform.Engaged() ? Transform.Manner() : static_cast<TransformManner>(Gizmo.Manner);
    if (Gizmo.Shown && !Transform.Engaged() && ActiveSelection.Standing())
    {
        GizmoScreenBasis Screen = {};
        if (ResolveGizmoScreenBasis(Camera, Extent, ActiveSelection.Position, Screen))
            ResolvedHandle = ResolveUniversalGizmoHandle(Screen, ActiveGizmoManner, Pointer.PositionX, Pointer.PositionY);
    }
    if (HoveredHandle != nullptr)
        *HoveredHandle = ResolvedHandle;

    if (ResolvedHandle != GizmoHandle::None)
        HoveredSelection = {};

    // 🔴 Clicking empty viewport space is the standard CAD deselect gesture.
    if (!PointerTaken && !Transform.Engaged() && Pointer.ContactPressed &&
        ResolvedHandle == GizmoHandle::None && !HoveredSelection.Standing())
    {
        if (!Modifiers.Shifted)
        {
            SelectionSet.Clear();
            SemanticSelection = {};
            ActiveSelection = {};
            PointerTaken = true;
        }
    }

    if (!PointerTaken && !Transform.Engaged() && Pointer.ContactPressed &&
        ResolvedHandle == GizmoHandle::None && HoveredSelection.Standing())
    {
        SetWorldPick(SelectionSet, HoveredSelection, Modifiers.Shifted);
        const WorldPick* UpdatedActive = SelectionSet.Active();
        SemanticSelection = UpdatedActive != nullptr ? *UpdatedActive : WorldPick{};
        ActiveSelection = SemanticSelection;
        PointerTaken = true;
    }

    const TransformCommandIntake Command =
        ResolveTransformCommand(TextInput.Intake, TextInput.IntakeCount, Transform.Engaged(), Transform.Manner());

    // 📝 Set by the start arm below so the engaged arm can tell "this session began on this very frame"
    //    from "a session was already standing and the artist has now tapped G again".
    bool StartedThisFrame = false;

    if (!Transform.Engaged() && !SelectionSet.Empty() && Extent.Encloses(Pointer.PositionX, Pointer.PositionY))
    {
        if (ResolvedHandle == GizmoHandle::None && !PointerTaken && Pointer.ContactHeld &&
            !Pointer.ContactPressed && (std::fabs(Pointer.TravelX) + std::fabs(Pointer.TravelY)) > 0.0f &&
            HoveredSelection.Standing())
        {
            PointerTaken = StartWorldSketchTransformSession(Declared, Camera, Extent,
                                                           Pointer.PositionX, Pointer.PositionY,
                                                           SelectionSet,
                                                           TransformRestriction::Free,
                                                           false, Transform, true);
        }

        if (ResolvedHandle != GizmoHandle::None && Pointer.ContactPressed)
        {
            PointerTaken = StartWorldSketchTransformSession(Declared, Camera, Extent,
                                                           Pointer.PositionX, Pointer.PositionY,
                                                           SelectionSet,
                                                           ResolveHandleRestriction(ResolvedHandle),
                                                           false, Transform, true,
                                                           ResolveHandleManner(ResolvedHandle));
        }
        else if (Command.StartRequested)
        {
            const_cast<GizmoOptions&>(Gizmo).Manner = static_cast<GizmoManner>(Command.StartManner);
            const bool Slide = Command.StartManner == TransformManner::Move
                            && ResolveSlideRequested(Command.MoveTapCount,
                                                     SessionMilliseconds,
                                                     LastGPressedMilliseconds,
                                                     ActiveSelection.Standing());
            if (Command.MoveTapCount > 0u)
                LastGPressedMilliseconds = SessionMilliseconds;
            const TransformRestriction Restriction =
                Slide ? TransformRestriction::Curve
                      : (Command.StartManner == TransformManner::Rotate
                           ? TransformRestriction::Screen : TransformRestriction::Free);
            PointerTaken = StartWorldSketchTransformSession(Declared, Camera, Extent,
                                                           Pointer.PositionX, Pointer.PositionY,
                                                           SelectionSet, Restriction, Slide,
                                                           Transform, false, Command.StartManner);
            StartedThisFrame = Transform.Engaged();
        }
    }

    if (Transform.Engaged())
    {
        // 🔴 ONE G BECAME A SLIDE. The arm above stamps `LastGPressedMilliseconds = SessionMilliseconds`
        //    when it starts the move, and this arm then asked the same question again in the SAME frame —
        //    one tap, zero milliseconds since the last tap, which is inside the 350 ms window. Every plain
        //    G therefore satisfied the double-tap gesture instantly and locked itself to a curve tangent,
        //    which is why a single G moved along the geometry instead of following the mouse. A frame that
        //    has just started a session has already spent its taps, so this arm must not read them again.
        const bool SlideRequested = !StartedThisFrame
                                 && ResolveSlideRequested(Command.MoveTapCount,
                                                          SessionMilliseconds,
                                                          LastGPressedMilliseconds,
                                                          Transform.Target.Standing());
        if (Command.MoveTapCount > 0u && !StartedThisFrame)
            LastGPressedMilliseconds = SessionMilliseconds;

        if (SlideRequested)
        {
            Transform.Restriction() = TransformRestriction::Curve;
            Transform.SlideAlongCurve() = true;
            if (Transform.Target.Standing())
                Transform.AxisDirection = ResolveWorldCurveSlideDirection(
                    Declared, Transform.Target.Curve, Transform.Target.Position);
        }
        else if (Command.RestrictionRequested)
        {
            Transform.Restriction() = Command.Restriction;
            Transform.SlideAlongCurve() = false;
        }

        if (Command.NumericAppend[0] != '\0')
            AppendTransformNumericRun(Transform.Standing.Numeric, TransformNumericLimit, Command.NumericAppend);
        if (TextInput.BackspacePressed)
            RetractTransformCommand(Transform.Standing);
        if (TextInput.DeletePressed)
            ClearTransformNumeric(Transform.Standing);

        if (TextInput.CancelPressed)
        {
            CancelWorldSketchTransformSession(Declared, Transform);
            RefreshWorldSelectionSet(Declared, SelectionSet);
            const WorldPick* CurrentActive = SelectionSet.Active();
            SemanticSelection = CurrentActive != nullptr ? *CurrentActive : WorldPick{};
            PointerTaken = true;
        }
        else
        {
            UpdateWorldSketchTransformSession(Camera, Extent,
                                             Pointer.PositionX, Pointer.PositionY,
                                             Declared, Transform);
            RefreshWorldSelectionSet(Declared, SelectionSet);
            const WorldPick* CurrentActive = SelectionSet.Active();
            SemanticSelection = CurrentActive != nullptr ? *CurrentActive : WorldPick{};
            PointerTaken = true;

            if (Transform.AwaitingRelease)
            {
                if (Pointer.ContactReleased)
                    CommitWorldSketchTransformSession(Transform);
            }
            else if (TextInput.AcceptPressed || Pointer.ContactPressed)
            {
                CommitWorldSketchTransformSession(Transform);
            }
        }
    }
}

} // namespace Slate
