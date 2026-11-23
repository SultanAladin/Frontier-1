//============================================================================================================================================
//                                                            CAMERAPROJECTION.H
//============================================================================================================================================
// 🧩 Where the viewer is and how a document position becomes a display position — one answer, read by twelve documents.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TWO PROJECTIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which of `46` §3's two projections a camera declares.
/// note  ⚠️ Dolly and zoom are different edits and the enumeration keeps them apart: dolly amends the placement
///        and changes what occludes what, zoom amends the extent parameter below and does not. An application
///        that binds one control to whichever is convenient produces an artist who cannot say why their
///        composition changed.
/// tag   guarantee
enum class ProjectionSubject : std::uint32_t
{
    Perspective     = 0u,   // [-] - angular field; the workspace
    Parallel        = 1u,   // [-] - linear extent; orthographic references
    ProjectionCount = 2u    // [-] - the closed count, never a projection
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CLIPPING INTERVAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The interval a camera resolves depth across.
/// note  🔴 One declaration, not two loose numbers. A nearest value that has crossed above the furthest is a
///        frustum with no interior, and the symptom is an empty workspace with no error anywhere — which is why
///        `IntervalValid` is asked before the projection is derived rather than after it produces nothing.
/// note  ⚠️ Spelled `Nearest` and `Furthest` rather than the obvious pair, because `near` and `far` are macros
///        the host toolchain still defines and a member of either spelling does not survive preprocessing.
/// tag   nonallocating, nonthrowing
struct ClippingInterval
{
    double  Nearest  = 1.0;        // [mm] - strictly positive
    double  Furthest = 100000.0;   // [mm] - strictly greater than Nearest

    /// 🧩 Whether the interval has an interior at all.
    /// cost  ✔️
    constexpr bool IntervalValid() const
    {
        return Nearest > 0.0 && Furthest > Nearest;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE CAMERA
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Everything one camera declares — `46` §2's table as storage.
/// note  🔴 Exposure is held **here**, in the document, because it is an authored decision about the image and
///        travels with the work. The display space is not, and `36` §2 rules that separately. `00` §10 conflict
///        33 records the two answers this used to have.
/// note  🔴 The focus position is declared and never inferred from what the view happens to meet. An orbit centre
///        derived from whatever the ray strikes moves when the artist orbits past a gap, and the object they were
///        inspecting leaves the display.
/// tag   nonallocating, nonthrowing
struct CameraSpecification
{
    DecomposedTransform  Placement        = {};                              // [mm]  - position and rotation, 64-bit
    ProjectionSubject    Projected        = ProjectionSubject::Perspective;  // [-]
    double               ExtentParameter  = 45.0;                            // [deg] - angular field, or [mm] linear
    ClippingInterval     Clipping         = {};                              // [mm]
    double               SensorProportion = 1.0;                             // [-]   - width to height; never assumed
    double               Exposure         = 0.0;                             // [EV]  - authored; `66` §2 reads it
    DocumentPosition     FocusPosition    = {};                              // [mm]  - declared, never inferred
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE VIEW PROJECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Document space to view-relative space, per `02` §3.2 — the rebasing origin and the matrices around it.
/// note  🔴 The translation is **not** in `ViewRotation`. It is the rebasing subtraction, performed at 64-bit by
///        `Rebase` before anything narrows, which is the whole of `02` §3.2's guarantee. A view matrix carrying
///        the camera's document position would narrow a billion-millimetre coordinate and lose the millimetre.
/// tag   nonallocating, nonthrowing
struct ViewProjection
{
    DocumentPosition    ViewOrigin   = {};   // [mm] - the rebasing origin; the camera's own position
    ProjectedTransform  ViewRotation = {};   // [-]  - the placement's rotation, conjugated
    ProjectedTransform  Projected    = {};   // [-]  - the projection alone
    ProjectedTransform  Composed     = {};   // [-]  - Projected × ViewRotation, over rebased positions
};

/// 🧩 Derives the view projection of one declared camera.
/// in    Declaring  [-]  the camera
/// out   Result    [-]  refuses with ContentUnsupported for an invalid clipping interval, a non-positive sensor
///                       proportion, or an extent parameter with no interior
/// note  📐 Depth is reversed — `NearPlaneDepth` at the nearest plane, `FarPlaneDepth` at the furthest. The
///        constants live in `Foundation/` because `16` compares against them, `30` marches against them and `80`
///        depth-tests against them; one document reversing its own comparison in isolation produces geometry that
///        vanishes rather than geometry that sorts wrongly.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
Deliver<ViewProjection> Derive(const CameraSpecification& Declaring);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE FRUSTUM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One bounding plane, in view-relative space, with the interior on the non-negative side.
/// tag   nonallocating, nonthrowing
struct FrustumPlane
{
    double  NormalX  = 0.0;   // [-]  - unit length
    double  NormalY  = 0.0;   // [-]
    double  NormalZ  = 0.0;   // [-]
    double  Constant = 0.0;   // [mm] - signed distance of the origin from the plane
};

/// 🧩 The six planes `16` culls against, derived from the composed projection.
/// note  🔴 Planes are pushed **outward** by `FrustumOutwardMargin`, matching `38` §6 and `40` §6. An
///        inward-rounded plane culls geometry the camera can see, and the artist meets it as a surface that
///        disappears along one edge of the display.
/// note  📐 Extraction is by the standard row-sum identity over the composed matrix: a clip-space inequality is a
///        linear form in the rebased position, and the four coefficients of that form are the plane. Deriving the
///        planes from the eight resolved corners instead is the same answer computed less exactly and more slowly.
/// tag   owning
class FrustumSpace
{
public:

    static constexpr std::uint32_t PlaneCount = 6u;   // [-] - four sides, nearest and furthest

    /// 🧩 Derives all six planes from a view projection.
    /// in    Projected  [-]  as `Derive` produced it
    /// post  every plane is unit-normalised and pushed outward
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeriveFrustumPlanes(const ViewProjection& Projected);

    /// 🧩 Classifies one document-space extent against the frustum.
    /// in    Minimum      [mm]  the extent's lower corner, in document space
    /// in    Maximum   [mm]  its upper corner
    /// out   Overlap    [-]   +1 wholly inside, 0 straddling a plane, −1 wholly outside
    /// note  🔴 The extent is rebased at 64-bit before it is narrowed — `02` §3.2 — so an extent a billion
    ///        millimetres from the document origin classifies against the camera rather than against the origin.
    /// note  📐 Answered by the two extremal corners along each plane's normal rather than by all eight. The
    ///        corner furthest along the normal decides exclusion and the corner least along it decides
    ///        straddling; the remaining six carry no additional fact.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::int32_t Classify(DocumentPosition Minimum, DocumentPosition Maximum) const;

    /// 🧩 Whether one document-space position lies inside every plane.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Contains(DocumentPosition Subject) const;

    /// 🧩 One derived plane, for whoever presents the frustum.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const FrustumPlane& Plane(std::uint32_t PlaneIndex) const;

private:

    FrustumPlane      Planes[PlaneCount] = {};      // [-]  - leftward, rightward, lower, upper, furthest, nearest
    DocumentPosition  RebasingOrigin     = {};      // [mm] - carried so Classify may take document positions
    bool              PlanesDerived      = false;   // [-]  - Construct has delivered
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     NAVIGATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which navigation gesture is open.
/// tag   guarantee
enum class NavigationSubject : std::uint32_t
{
    Orbit           = 0u,   // [-] - rotation, and position to preserve the distance, about the focus
    Pan             = 1u,   // [-] - position, in the camera plane
    Dolly           = 2u,   // [-] - position along the view direction
    Zoom            = 3u,   // [-] - the projection's extent parameter
    NavigationCount = 4u    // [-] - the closed count, never a gesture
};

// 📝 The four rates below convert a pointer displacement in display pixels into the gesture's own measure. They
//    are declared here and read by nothing else, so `00` §2's rule places them in this unit rather than in
//    `Foundation/`. Pan and dolly are additionally scaled by the distance to the focus, so a gesture feels the same
//    whether the artist is inspecting a rivet or framing a building.
inline constexpr double OrbitRadiansPerPixel  = 0.006;   // [rad/px]
inline constexpr double PanFractionPerPixel   = 0.0018;  // [-/px] - of the focus distance
inline constexpr double DollyFractionPerPixel = 0.004;   // [-/px] - of the focus distance
inline constexpr double ZoomFractionPerPixel  = 0.004;   // [-/px] - of the extent parameter

/// 🧩 One navigation gesture, following `10` §2.4's lifecycle exactly.
/// note  🔴 A gesture Seals **one** transaction. `10` §2.4 is not relaxed here: an orbit recorded per pointer
///        sample would fill `RevisionSequence` with a thousand states, and `84` would present a scrub bar that is
///        almost entirely camera motion.
/// note  ⚠️ Nothing is recorded between Open and Seal. The amended camera is readable so the workspace can be
///        presented from it, and the caller commits the sealed specification as its own transaction.
/// tag   owning
class NavigationSequence
{
public:

    /// 🧩 Opens a gesture against a standing camera, holding its prior specification.
    /// out   Result  [-]  refuses with HostDenied when a gesture is already open
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Open(NavigationSubject Declaring, const CameraSpecification& Current);

    /// 🧩 Amends the open gesture by one pointer displacement.
    /// in    DisplacementX   [px]  horizontal displacement since the last amendment
    /// in    DisplacementY  [px]  vertical displacement since the last amendment
    /// out   Result             [-]   refuses with HostDenied when no gesture is open
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Amend(double DisplacementX, double DisplacementY);

    /// 🧩 Ends the gesture with no effect, returning the prior specification.
    /// out   Result  [-]  refuses with HostDenied when no gesture is open
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<CameraSpecification> Abandon();

    /// 🧩 Ends the gesture, returning the specification the caller commits as one transaction.
    /// out   Result  [-]  refuses with HostDenied when no gesture is open
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<CameraSpecification> Seal();

    /// 🧩 The camera as the gesture has amended it, for presentation while it is open.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const CameraSpecification& Amended() const;

    /// 🧩 Whether a gesture is open.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool GestureOpen() const;

private:

    CameraSpecification  PriorCamera   = {};                        // [-] - held at Open, restored at Abandon
    CameraSpecification  AmendedCamera = {};                        // [-] - what Amend writes
    NavigationSubject    Declaring     = NavigationSubject::Orbit;  // [-] - which gesture
    bool                 OpenDeclared  = false;                     // [-] - Open delivered, Seal has not
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      FRAMING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Produces a placement containing one document-space extent.
/// in    Current  [-]   the camera whose projection and rotation are kept
/// in    Minimum     [mm]  the extent's lower corner
/// in    Maximum  [mm]  its upper corner
/// out   Result   [-]   refuses with ContentUnsupported for an inverted extent or an invalid projection
/// note  🔴 The **placement only** is produced. The projection's extent parameter is left as the artist set it,
///        because framing that also changed the field would be framing that changed the composition.
/// note  ⚠️ For a parallel projection the placement is centred and nothing else can be done — the extent
///        parameter is what decides containment there, and this routine is forbidden to touch it. `46` §5 records
///        that as the declared behaviour rather than an omission.
/// note  📐 The distance is solved against the extent's bounding sphere and the lesser of the two half-angles, so
///        the extent is contained on both axes rather than on whichever happens to be wider.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
Deliver<DecomposedTransform> Frame(const CameraSpecification& Current,
                                   DocumentPosition           Minimum,
                                   DocumentPosition           Maximum);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CAMERA
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One camera owner — its specification, and the two derivations reconciled from it.
/// note  🔴 A camera is an **owner of the document population**. It registers in `12`, appears in the outliner,
///        attaches through `AttachmentFollows` and is manipulated by `78`. A camera held outside the population
///        is one the artist cannot select, name, group, attach or undo.
/// note  🔴 Being an owner does not make a camera shaded. It writes no `VisibilityIndex`, exactly as `44`'s
///        illuminants do not; its presence in the workspace is an `80` overlay at `08` §3 ⑩.
/// note  ⚠️ `Reconcile` is the only writer of the two derivations, and it is owed after every amendment. A
///        consumer reading the frustum without it reads the frustum of the camera as it stood last tick, which is
///        a cull against a view the artist has already left.
/// tag   owning
class CameraProjection
{
public:

    /// 🧩 Declares which owner this camera is, and its initial specification.
    /// out   Result  [-]  refuses with IdentityStale for an undeclared identity
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Declare(OwnerIdentity Subject, const CameraSpecification& Declaring);

    /// 🧩 Amends the specification, leaving the derivations owed.
    /// out   Result  [-]  refuses with IdentityStale before Declare has delivered
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Amend(const CameraSpecification& Amending);

    /// 🧩 Declares the display's drawable extent, from which the sensor proportion follows.
    /// out   Result  [-]  refuses with ContentUnsupported for a zero extent on either axis
    /// note  ⚠️ The proportion is derived from the display and is not stored as an authored property. A document
    ///        carrying its author's window proportion opens framed for their monitor and not for the artist's.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> DeclareDisplayExtent(std::uint32_t Width, std::uint32_t Height);

    /// 🧩 Re-derives the view projection and the frustum.
    /// out   Result  [-]  carries `Derive`'s refusal when the specification cannot be projected
    /// post  the frustum and the view projection describe the current specification
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Reconcile();

    const CameraSpecification&  Declared() const;
    const ViewProjection&       Projected() const;
    const FrustumSpace&         Frustum() const;

    /// 🧩 Which owner this camera is.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    OwnerIdentity Owner() const;

    /// 🧩 The authored exposure `66` §2 reads.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double Exposure() const;

    /// 🧩 Whether an amendment has been made that `Reconcile` has not yet absorbed.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool DerivationOwed() const;

private:

    CameraSpecification  Specification   = {};      // [-] - as declared and amended
    ViewProjection       DerivedView     = {};      // [-] - Reconcile writes it
    FrustumSpace         DerivedFrustum  = {};      // [-] - Reconcile writes it
    OwnerIdentity     CameraOwner  = {};      // [-] - the population slot
    bool                 ReconcileOwed   = true;    // [-] - an amendment stands unabsorbed
};

// 📐 Placement composition is `02` §3.1's decomposed form and the frustum classification is integer-decided over
//    exactly compared coordinates; the projection derivation reads circular functions and is therefore Bounded.
//    The component claims Bounded, because `00` §3's transitivity rule forbids claiming the stronger guarantee
//    over a body that also produces the weaker one.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
