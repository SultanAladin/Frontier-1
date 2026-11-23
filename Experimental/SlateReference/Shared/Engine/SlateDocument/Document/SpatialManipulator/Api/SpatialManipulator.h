//============================================================================================================================================
//                                                          SPATIALMANIPULATOR.H
//============================================================================================================================================
// 🧩 `78` — one manipulator that moves, rotates and scales everything movable, and the grips the artist grasps to do it.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/CameraProjection/Api/CameraProjection.h"
#include "SlateDocument/Document/PointerIntersection/Api/PointerIntersection.h"
#include "SlateDocument/Document/PrimitiveStructure/Api/PrimitiveStructure.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"
#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE THREE AXES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which axis of the reference orientation a grip addresses.
/// note  📝 The ordinal **is** the axis, so a grip's axis indexes a basis directly rather than being switched on.
///        The screen entry is last because it addresses no basis axis at all — it is the camera plane, and `78` §3
///        lists it as a constraint rather than as a direction.
/// tag   guarantee
enum class ManipulationAxis : std::uint32_t
{
    AxisX   = 0u,   // [-] - the reference orientation's first axis
    AxisUp      = 1u,   // [-] - its second
    AxisY  = 2u,   // [-] - its third
    AxisScreen  = 3u,   // [-] - the camera plane; `78` §3's screen and free constraints
    AxisCount   = 4u    // [-] - the closed count, never an axis
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT A GRIP DOES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which of the three transforms a grip edits, and along how many axes it edits it.
/// note  🔴 One manipulator addresses all three — `78` §5's first gate. Three separate manipulators, each shown
///        alone, make every edit begin with a mode change the artist has to remember they are in; and the mode they
///        are in is invisible the moment the manipulator leaves the display.
/// note  📝 A plane translation is held apart from an axis translation rather than being two axis translations at
///        once. The drag resolves against a plane rather than against a line, and `78` §2 fixes that plane at Open
///        — two line solves would each re-derive their own and the two would disagree the moment the camera moved.
/// tag   guarantee
enum class ManipulationSubject : std::uint32_t
{
    Translate       = 0u,   // [-] - along one axis of the reference orientation
    PlaneTranslate  = 1u,   // [-] - within the plane of the other two
    Scale           = 2u,   // [-] - along one axis, about the manipulator's own origin
    Rotate          = 3u,   // [-] - about one axis, in the plane of the other two
    SubjectCount    = 4u    // [-] - the closed count, never a subject
};

/// 🧩 What the drag resolves against, fixed at Open and never re-derived — `78` §3.
/// tag   guarantee
enum class ConstraintSubject : std::uint32_t
{
    AxisConstrained   = 0u,   // [-] - one axis of the reference orientation
    PlaneConstrained  = 1u,   // [-] - two axes of it
    ScreenConstrained = 2u,   // [-] - the camera plane; translation and scale only
    Unconstrained     = 3u,   // [-] - the camera plane, with no axis displayed
    ConstraintCount   = 4u    // [-] - the closed count, never a constraint
};

/// 🧩 Whose axes the manipulator is drawn along and the drag is resolved in — `78` §3's second table.
/// note  🔴 The surface reference is what makes placement manipulation usable. Dragging a decal along document axes
///        moves it off the surface it sits on; dragging along the surface reference slides it across the surface,
///        which is what the gesture means and what the artist expects to have happened.
/// tag   guarantee
enum class ReferenceOrientation : std::uint32_t
{
    DocumentAxes   = 0u,   // [-] - the document's own axes
    OwnerAxes   = 1u,   // [-] - the manipulated owner's own orientation
    SurfaceAxes    = 2u,   // [-] - the intersected surface's orientation, from `74`
    PlacementAxes  = 3u,   // [-] - the placement's own axes on the surface it sits on
    OrientationCount = 4u  // [-] - the closed count, never an orientation
};

/// 🧩 What the manipulator is addressing — `78` §1's four targets.
/// note  🔴 A placement is manipulated in the space it is **stored** in, relative to its surface, and never in
///        document space. The manipulator is drawn in document space and the drag is projected back through the
///        attachment before it is applied. Applied in document space it would store an absolute transform and
///        `00` §10.1 ②'s zero-cost rows would each become "re-resolve everything".
/// tag   guarantee
enum class ManipulatedSubject : std::uint32_t
{
    Nothing        = 0u,   // [-] - no target; the manipulator is not presented
    OneOwner    = 1u,   // [-] - its own transform, composed downward through `AttachmentFollows`
    ManyOwners  = 2u,   // [-] - each own transform, about one shared origin
    OnePlacement   = 3u,   // [-] - its placing transform, relative to the surface — `72` §1
    OneCamera      = 4u,   // [-] - `46`'s projection origin
    TargetCount    = 5u    // [-] - the closed count, never a target
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       ONE GRIP
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One grabbable part of the manipulator, in the manipulator's own space.
/// note  🔴 A grip's geometry is declared in the manipulator's own space and scaled to a constant display extent
///        when it is laid out. A manipulator that scaled with its target vanishes on a small owner and fills
///        the workspace on a large one, and in both cases the artist can no longer grasp the axis they want.
/// note  📝 `Reach` and `HalfExtent` describe a capsule about the grip's own axis, and that capsule is what the
///        screen-space intersection in `78` §4 actually tests. Testing the generated triangles instead would make
///        a grip harder to hit exactly where it is thinnest, which is the tip of the cone the artist aims at.
/// tag   nonallocating, nonthrowing
struct ManipulationGrip
{
    ManipulationSubject   Edits          = ManipulationSubject::Translate;   // [-]  - what grasping it does
    ManipulationAxis      Addressed      = ManipulationAxis::AxisX;      // [-]  - which axis it addresses
    ColourSpecification   GripColour     = {};                              // [-]  - display-space; `80` §2's rule
    DocumentPosition      NearPosition   = {};                              // [-]  - the capsule's first end, in manipulator space
    DocumentPosition      FarPosition    = {};                              // [-]  - its second
    double                HalfExtent     = 0.0;                             // [-]  - the capsule's radius about that span
    PrimitiveSpecification Generated     = {};                              // [-]  - what is drawn for it — `78` §4
    DecomposedTransform   GripPlacement  = {};                              // [-]  - where that solid sits in manipulator space
    bool                  GripDeclared   = false;                           // [-]  - false for a grip this target does not offer
};

// 📝 The layout's proportions, in manipulator units where the axis length is one. Held as named constants rather
//    than written into the layout, because every one of them is a proportion another one is chosen against — the
//    scale grip's radius matches the cone's base so the two read as one continuous grip, and a change to either
//    that is not a change to both is visible immediately as a step in the middle of the axis.
inline constexpr double GripAxisLength      = 1.0;      // [-] - the reference length every other proportion is against
inline constexpr double GripTipReach        = 0.95;     // [-] - where the translate cone's tip sits
inline constexpr double GripConeRadius      = 0.06;     // [-] - the cone's base, and the scale grip's own radius
inline constexpr double GripConeLength      = 0.18;     // [-] - the cone from base to tip
inline constexpr double GripScaleLength     = 0.14;     // [-] - the scale grip's own length along the axis
inline constexpr double GripScaleInboard    = 0.28;     // [-] - how far inboard of the tip the scale grip sits
inline constexpr double GripPlaneHalfExtent = 0.08;     // [-] - the plane grip's half-extent on both of its axes
inline constexpr double GripArcRadius       = 0.589;    // [-] - the rotation arc's radius; the tip reach times 0.62
inline constexpr double GripArcBand         = 0.038;    // [-] - its half-width in the radial direction
inline constexpr double GripArcSweep        = 0.541;    // [rad] - 31 degrees, centred on the bisector of its two axes
inline constexpr double GripRingRadius      = 0.16;     // [-] - the central ring, which always faces the camera
inline constexpr double GripRingBand        = 0.008;    // [-] - the ring's own thickness

// 🔴 The manipulator is laid out to a constant fraction of the **view height** and not to a pixel count. `78` §6's
//    third open row is the grip extent in display pixels at high display density, and a fraction is the answer that
//    does not have to be revisited when it is answered: the same fraction is the same apparent size on every
//    display, and `14`'s scaling policy then multiplies it rather than replacing it.
inline constexpr double GripViewFraction    = 0.16;     // [-] - the axis length, as a fraction of the view height

// 📝 `78` §6's first open row — whether increment snapping is per tool or global — is not decided here. These are
//    the increments a snapped drag uses; where the declaration lives is `76`'s question either way.
inline constexpr double SnapTranslation     = 0.25;     // [mm]  - one snapped step of a translation
inline constexpr double SnapScaleStep       = 0.1;      // [-]   - one snapped step of a scale factor
inline constexpr double SnapRotationStep    = 0.0872664625997165;   // [rad] - five degrees
inline constexpr double ScaleFactorMinimum    = 0.05;     // [-]   - a scale is never dragged through zero into inversion

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE LAYOUT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The manipulator's grips as they stand for one target, one reference orientation and one camera.
/// note  🔴 Laid out afresh whenever the target, the orientation or the camera changes, and read unamended between
///        those. A layout re-derived per pointer sample would move the grips under a drag that is already
///        addressing one of them, which is the same defect `78` §2 refuses for the drag plane.
/// tag   owning
class ManipulationLayout
{
public:

    static constexpr std::uint32_t GripLimit = 16u;   // [-] - three axes × four subjects, plus the ring

    /// 🧩 Lays the grips out about one origin, in one reference orientation, at a constant display extent.
    /// in    Origin       [mm]  where the manipulator sits, in document space
    /// in    Orientation  [-]   the reference orientation's rotation, already resolved by the caller
    /// in    Camera       [-]   the camera the extent is held constant against; its derivation must not be owed
    /// in    Addressing   [-]   which of `78` §1's targets is being manipulated
    /// out   Result      [-]   refuses with ContentUnsupported for the closed target count and for a camera whose
    ///                          derivation is owed
    /// post  every grip this target offers is declared; the rest stand undeclared and are never intersected
    /// note  🔴 A camera owing a reconciliation refuses rather than laying out against the standing derivation.
    ///        `46` §7 makes `Reconcile` the only writer of it, and grips laid out through last tick's projection
    ///        sit where the artist was looking before they moved — which is exactly where they will click.
    /// note  📝 Which grips a target offers is decided here and not by the caller. A camera offers no scale grip
    ///        because `46` has no scale to edit, and a caller deciding that would be a second place the rule lives.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Layout(DocumentPosition          Origin,
                         RotationQuaternion        Orientation,
                         const CameraProjection&   Camera,
                         ManipulatedSubject        Addressing);

    /// 🧩 Which grip one pointer position grasps — `78` §4's own intersection, before `74` is consulted.
    /// in    Camera         [-]   the camera the pointer was reported against
    /// in    PointerX   [px]  the display's first axis, zero at the left edge
    /// in    PointerY  [px]  its second, zero at the top edge
    /// in    DisplayX   [px]  the drawable extent the position was reported against
    /// in    DisplayY  [px]  ?
    /// out   Result        [-]   the grip ordinal grasped; refuses with ContentUnsupported when the pointer
    ///                            grasps no grip, and when no layout stands
    /// note  🔴 `78` §4: this is a **separate** screen-space test against the grips' own geometry, at precedence 2
    ///        in `14` §4.2, and it runs **before** `74` is consulted at all. The manipulator writes no
    ///        `VisibilityIndex`, so `74` cannot pick it and `26` cannot outline it — asking `74` first would
    ///        therefore return whatever stands behind the grip, and the artist would select through it.
    /// note  📝 The nearest grip along the ray wins, and ties go to the lower ordinal. The layout orders the axis
    ///        grips before the plane and rotation ones, so a pointer over the overlap of a cone and an arc grasps
    ///        the cone — which is the smaller target and therefore the one that was aimed at.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Grasp(const CameraProjection& Camera,
                                 double                  PointerX,
                                 double                  PointerY,
                                 std::uint32_t           DisplayX,
                                 std::uint32_t           DisplayY) const;

    /// 🧩 One laid-out grip.
    /// out   Result  [-]  refuses with ContentUnsupported outside the laid-out count and for an undeclared grip
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const ManipulationGrip*> Resolve(std::uint32_t GripIndex) const;

    /// 🧩 Every laid-out grip, for whoever records them.
    /// note  📝 Recorded in `08` §3 ⑪ — the depth-free overlay recording `80` already declares, at
    ///        `OverlaySubject::Manipulator`. The manipulator contributes no recording of its own: a second one
    ///        would be a second place `08`'s ordering is declared, and the two would order differently the first
    ///        time either was amended.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<ManipulationGrip>& Grips() const;

    /// 🧩 Where the manipulator sits and how it is turned, as the last layout placed it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    DocumentPosition   Origin() const;
    RotationQuaternion Orientation() const;

    /// 🧩 The extent one manipulator unit spans in document space at the layout's distance.
    /// note  📝 What makes the display extent constant. Read by the recording so the grips are drawn at the size
    ///        they were laid out at, and by nothing else.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double UnitExtent() const;

    /// 🧩 Whether a layout stands at all.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool LayoutCurrent() const;

    /// 🧩 Discards the layout. The manipulator is not presented until one is laid out again.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

private:

    std::vector<ManipulationGrip>  Declared;                                        // [-]  - in grasp precedence order
    DocumentPosition               LaidOrigin        = {};                          // [mm] - document space
    RotationQuaternion             LaidOrientation   = {};                          // [-]  - the reference orientation
    ManipulatedSubject             LaidTarget        = ManipulatedSubject::Nothing; // [-]  - what it was laid out for
    double                         LaidUnitExtent    = 1.0;                         // [mm] - one manipulator unit
    bool                           LayoutDeclared    = false;                       // [-]  - false until Layout delivered
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE DRAG
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one amendment of a drag has produced, in the space the target is stored in.
/// note  🔴 The amendment is reported as a **displacement, a factor and a rotation** rather than as a finished
///        transform. `78` §1's four targets each compose it differently — a placement composes it against its
///        surface, an owner against its attachment — and a finished transform would have had to choose one.
/// tag   nonallocating, nonthrowing
struct ManipulationAmendment
{
    DocumentPosition    Displacement  = {};                  // [mm] - what a translation has moved by
    RotationQuaternion  Turned        = {};                  // [-]  - what a rotation has turned by, about the drag axis
    double              ScaleX    = 1.0;                 // [-]  - the factor on the reference orientation's first axis
    double              ScaleUp       = 1.0;                 // [-]  - its second
    double              ScaleY   = 1.0;                 // [-]  - its third
    double              TurnedRadians = 0.0;                 // [rad] - the same rotation as a signed angle, for a readout
    ManipulationSubject Edited        = ManipulationSubject::Translate;   // [-] - which of the three this amendment is
};

/// 🧩 One manipulation, following `10` §2.4's lifecycle exactly — `78` §2's correspondence.
/// note  🔴 The drag resolves against a plane or axis **fixed at Open** and never re-derived per sample. Re-derived
///        from the current pointer position it makes the manipulated object chase the cursor with increasing gain,
///        which the artist reads as the manipulator being slippery rather than as the plane being wrong.
/// note  🔴 Nothing is recorded between Open and Seal — `78` §5. A transaction per pointer sample fills
///        `RevisionSequence` with positions the artist never meant to stop at, and undo then steps back one pixel
///        at a time. Exactly `10` §2.4, unrelaxed, and the same rule `46`'s navigation and `72`'s positioning keep.
/// note  ⚠️ Pointer capture is held for the whole drag through `76` §3 and is **not** taken here. A drag that began
///        on a grip continues to address that grip after the cursor leaves the workspace, and the capture that
///        makes that true has exactly one owner.
/// tag   owning
class ManipulationSequence
{
public:

    /// 🧩 Opens a manipulation against one grasped grip, fixing the axis or plane it resolves against.
    /// in    Grasped        [-]   the grip the pointer grasped
    /// in    Laid           [-]   the layout it was grasped from
    /// in    Camera         [-]   the camera; read once here and never again during the drag
    /// in    PointerX   [px]  where the pointer stood when the grip was grasped
    /// in    PointerY  [px]  ?
    /// in    DisplayX   [px]  the drawable extent
    /// in    DisplayY  [px]  ?
    /// out   Result        [-]   refuses with HostDenied when a drag is already open, and with ContentUnsupported
    ///                            for an undeclared grip or a pointer that resolves no position on the fixed plane
    /// post  🔴 the drag axis or plane is fixed; nothing is recorded until Seal
    /// note  🔴 The camera is read **here and once**. `78` §2's rule is about the plane, and a plane fixed from a
    ///        camera that is re-read each sample is a plane that moves whenever the artist orbits mid-drag.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Open(const ManipulationGrip&   Grasped,
                       const ManipulationLayout& Laid,
                       const CameraProjection&   Camera,
                       double                    PointerX,
                       double                    PointerY,
                       std::uint32_t             DisplayX,
                       std::uint32_t             DisplayY);

    /// 🧩 Amends the open drag by one pointer position, against the axis or plane Open fixed.
    /// in    PointerX    [px]  where the pointer stands now
    /// in    PointerY   [px]  ?
    /// in    DisplayX    [px]  the drawable extent
    /// in    DisplayY   [px]  ?
    /// in    SnapDeclared    [-]   true snaps the amendment to the declared increment
    /// out   Result         [-]   refuses with HostDenied when no drag is open, and with ContentUnsupported for a
    ///                             pointer that resolves no position against the fixed plane
    /// post  nothing is recorded; the amendment is readable and the target is unamended until Seal
    /// note  📝 The pointer's position is taken rather than its displacement since the last sample. Accumulating
    ///        displacements accumulates their rounding too, and a snapped drag would then drift off its own
    ///        increment over a long gesture — which reads as the snapping having been switched off.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Amend(double        PointerX,
                        double        PointerY,
                        std::uint32_t DisplayX,
                        std::uint32_t DisplayY,
                        bool          SnapDeclared);

    /// 🧩 Ends the drag with no effect. The caller restores what stood at Open.
    /// out   Result  [-]  refuses with HostDenied when no drag is open
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<ManipulationAmendment> Abandon();

    /// 🧩 Ends the drag, returning the amendment the caller commits as **one** transaction.
    /// out   Result  [-]  refuses with HostDenied when no drag is open
    /// post  🔴 exactly one transaction enters `RevisionSequence`, sealed by the caller — `78` §2 and §5
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<ManipulationAmendment> Seal();

    /// 🧩 The amendment as the drag stands, for the workspace to present while it is open.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const ManipulationAmendment& Amended() const;

    /// 🧩 Whether a drag is open.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool DragOpen() const;

    /// 🧩 What the open drag resolves against, as Open fixed it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    ConstraintSubject Constrained() const;

    /// 🧩 Which grip the open drag addresses.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const ManipulationGrip& Grasped() const;

private:

    ManipulationGrip       GraspedGrip;                                            // [-]  - as it stood at Open
    ManipulationAmendment  Current;                                               // [-]  - what the drag has amended
    ConstraintSubject      FixedConstraint = ConstraintSubject::AxisConstrained;   // [-]  - fixed at Open

    // 🔴 The camera is **held** rather than passed to each amendment, which is what makes the note above
    //    enforceable rather than advisory. `Amend` takes no camera, so an artist who orbits mid-drag cannot move
    //    the plane the drag resolves against — the projection here is the one Open read, for the whole gesture.
    CameraProjection       HeldCamera      = {};    // [-]  - as it stood at Open, and never re-read

    DocumentPosition       DragOrigin      = {};      // [mm] - the manipulator's origin, held from Open
    double                 AxisXSpan   = 0.0;     // [-]  - the drag axis in document space, unit length
    double                 AxisUpSpan      = 0.0;     // [-]
    double                 AxisYSpan  = 0.0;     // [-]
    double                 PlaneXSpan  = 0.0;     // [-]  - the drag plane's first span, for a plane drag
    double                 PlaneUpSpan     = 0.0;     // [-]
    double                 PlaneYSpan = 0.0;     // [-]
    double                 ReferenceX  = 0.0;     // [-]  - the angle's reference direction, for a rotation
    double                 ReferenceUp     = 0.0;     // [-]
    double                 ReferenceY = 0.0;     // [-]

    double                 OpenParameter   = 0.0;     // [mm] - the axis parameter the pointer stood at, at Open
    double                 OpenAngle       = 0.0;     // [rad] - the angle it stood at, for a rotation
    DocumentPosition       OpenPlanePoint  = {};      // [mm] - where it met the fixed plane, for a plane drag
    double                 UnitExtent      = 1.0;     // [mm] - one manipulator unit, held from the layout
    bool                   OpenDeclared    = false;   // [-]  - false until Open delivered
};

// 📐 The grasp test, the axis solve and the plane solve are all Bounded — they are 64-bit arithmetic over a ray and
//    a plane, and `02` §3's rebasing does not enter because every one of them runs in document space. Nothing here
//    is Exact: an angle is a transcendental and a scale factor is a quotient.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Bounded);

}   // namespace Slate
