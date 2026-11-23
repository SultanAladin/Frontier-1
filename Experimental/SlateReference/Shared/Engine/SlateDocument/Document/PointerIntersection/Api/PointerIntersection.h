//============================================================================================================================================
//                                                         POINTERINTERSECTION.H
//============================================================================================================================================
// 🧩 What is under the pointer — resolved on the host, every sample, as one tuple and never as several answers.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/CameraProjection/Api/CameraProjection.h"
#include "SlateDocument/Document/DecalProjection/Api/DecalProjection.h"
#include "SlateDocument/Document/RegistrationIndex/Api/RegistrationIndex.h"
#include "SlateDocument/Document/SpatialSubdivision/Api/SpatialSubdivision.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE PROJECTED RAY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One pointer position carried into document space as a ray.
/// note  🔴 The origin is at 64 bits and is the camera's own document position, so `02` §3.2's rebasing has not
///        happened and must not: this ray is traversed against `40`'s subdivision, which is in document space.
///        A ray rebased here would be a ray about the camera, and every extent it tested would be somewhere else.
/// note  📝 The direction is unit length for a perspective camera and for a parallel one alike, so the parameter
///        `40` returns is a document-space distance in both cases and the two order against each other.
/// tag   nonallocating, nonthrowing
struct ProjectedRay
{
    DocumentPosition  Origin     = {};    // [mm] - the camera's own position, or the parallel entry position
    double            DirectionX = 0.0;   // [-]  - unit length, in document space
    double            DirectionY = 0.0;   // [-]
    double            DirectionZ = 0.0;   // [-]
};

/// 🧩 Carries one display position into a document-space ray through a declared camera.
/// in    Camera          [-]   the camera; its derivation must not be owed
/// in    PointerX    [px]  the display's first axis, continuous, zero at the left edge
/// in    PointerY   [px]  its second, zero at the top edge
/// in    DisplayX    [px]  the drawable extent this pointer position was reported against
/// in    DisplayY   [px]
/// out   Result         [-]   refuses with ContentUnsupported for a zero display extent or a projection with no
///                             interior, and with HostDenied while the camera owes a reconciliation
/// note  🔴 The projection's own coefficients are read rather than a second unprojection being derived. `46` §3
///        applies `ClipCoordinateSignum` in its second row, so the display's downward-increasing coordinate is
///        already folded into the coefficient — a second inversion here would place every ray on the wrong side
///        of the horizon, and it would do so only for the vertical axis, which reads as a camera that is subtly
///        mis-aimed rather than as an inversion.
/// note  ⚠️ A camera owing a reconciliation refuses rather than projecting through the standing derivation. `46`
///        §7 makes `Reconcile` the only writer of it, and a ray cast through last tick's projection is a ray cast
///        at where the artist was looking before they moved.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
Deliver<ProjectedRay> ProjectPointerRay(const CameraProjection& Camera,
                                        double                  PointerX,
                                        double                  PointerY,
                                        std::uint32_t           DisplayX,
                                        std::uint32_t           DisplayY);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE ADMITTED SURFACE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one owner supplies beyond its geometry, so that a hit resolves a domain position and a placement.
/// note  🔴 The corner coordinates are **supplied** rather than read from `68`. A surface carrying an imported
///        domain addresses `TopologyStructure::Coordinates` and one that was unwrapped addresses
///        `ChartPartition::Coordinate`; the caller knows which and this component does not. Reaching for `68`
///        would also put a `SlateCompute` component in this file's Upstream, which the peer partition forbids —
///        the same reasoning `72`'s `ProjectPlacementExtent` already applies to the same run.
/// note  📝 Every field but the owner may be absent. An owner accepted with no topology is pickable by
///        extent and resolves no domain position, which is what an owner carrying no texturable surface is.
/// note  ⚠️ Nothing here is owned. The caller keeps the topology, the coordinate run and the placement
///        subdivision alive for as long as the owner stands, exactly as `40`'s `AcceptedOwner` requires of
///        its `BoundingStructure`.
/// tag   nonallocating, nonthrowing
struct AcceptedSurface
{
    OwnerIdentity                      Owner          = {};        // [-] - the population slot
    const TopologyStructure*              Imported          = nullptr;   // [-] - borrowed; null accepts no domain
    const std::vector<DomainCoordinate>*  CornerCoordinates = nullptr;   // [-] - borrowed; one per imported corner
    const AxisSpace*                      Placements        = nullptr;   // [-] - borrowed; `72`'s extents on it
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT A POINTER MEETS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The whole tuple one pointer sample resolves to — `74` §2's table, complete.
/// note  🔴 Resolved as **one** tuple rather than as several answers. `22` wants the domain position, `78` wants
///        the position and the orientation, `12` wants the owner and `72` wants the placement; resolving them
///        separately would traverse the same subdivision four times per pointer sample.
/// note  🔴 `PlacementResolved` is `74` §3's precedence 1 having fired. It is reported beside the surface rather
///        than instead of it, because `78` still needs the surface's orientation to build a manipulator plane
///        for the placement sitting on it.
/// tag   nonallocating, nonthrowing
struct ResolvedPointer
{
    OwnerIdentity  Owner          = {};                 // [-]  - `12` registration, `78` manipulation
    std::uint32_t     FaceIndex       = 0u;                 // [-]  - component selection
    std::uint32_t     CornerIndexs[3] = { 0u, 0u, 0u };     // [-]  - the fan triangle's corners
    double            Weights[3]        = { 0.0, 0.0, 0.0 };  // [-]  - barycentric, summing to one
    double            Distance          = 0.0;                // [mm] - along the supplied direction
    DocumentPosition  Position          = {};                 // [mm] - `78`'s manipulator plane
    SurfaceDirection  Orientation       = {};                 // [-]  - the hit face's own, in document space
    double            DomainX       = 0.0;                // [-]  - `22`'s stroke position
    double            DomainY      = 0.0;                // [-]
    std::uint32_t     PlacementIndex  = AbsentPlacement;    // [-]  - `72`'s ordinal where one was hit
    bool              DomainResolved    = false;              // [-]  - the owner accepted a coordinate run
    bool              PlacementResolved = false;              // [-]  - a placement contains the domain position
    bool              Resolved          = false;              // [-]  - anything was met at all
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE INTERSECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Picking — the one mechanism every consumer of the pointer reads.
/// note  🔴 `74` §1: answered on the **host**, against `40`'s subdivision, and never by reading back a device
///        target. `16` writes exactly this answer into `VisibilityIndex` one rotation late, and readback is
///        latent by the recording slot count while a stylus reports at hundreds of samples per second. A pointer
///        resolved against a target read back is resolved against where the cursor used to be — `22` §1.
/// note  ⚠️ The duplication of work against `16` is therefore deliberate and is the cheaper of the two options.
/// tag   owning
class PointerIntersection
{
public:

    /// 🧩 Accepts one owner's surface sources, or amends the ones already accepted at that identity.
    /// out   Result  [-]  refuses with IdentityStale for an undeclared identity, and with ContentUnsupported
    ///                     when a coordinate run disagrees with the topology's corner count
    /// note  📝 The coordinate count is confirmed here rather than at the hit. A run that is one corner short
    ///        reads past its end at whichever triangle happens to touch the last corner, which is a pick that is
    ///        correct almost everywhere.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Accept(const AcceptedSurface& Incoming);

    /// 🧩 Withdraws one owner's surface sources.
    /// out   Result  [-]  refuses with IdentityStale when the owner is not accepted
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Withdraw(OwnerIdentity Subject);

    /// 🧩 One accepted owner's sources.
    /// out   Result  [-]  refuses with IdentityStale when the owner is not accepted
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const AcceptedSurface*> Current(OwnerIdentity Subject) const;

    /// 🧩 Resolves one ray to the whole tuple.
    /// in    Projected     [-]  the ray, from `ProjectPointerRay`
    /// in    Subdivision   [-]  `40`'s document-space subdivision over owners
    /// in    Subsets       [-]  `12`'s registrations; locked and hidden owners are never traversed
    /// in    Placements    [-]  `72`'s declarations, for precedence 1
    /// out   Resolved      [-]  an unresolved tuple where nothing was met — `74` §3's precedence 3
    /// note  🔴 The traversal runs before the placement test and the precedence is unaffected by that. A
    ///        placement sits **on** a surface, so both always intersect and the surface must be found first to
    ///        produce the domain position the placement is tested against; §3's ordering is about which is
    ///        reported as picked, not about which is traversed first.
    /// note  🔴 A placement is confirmed through `ProjectIntoSource` after its extent contains the position.
    ///        `72`'s extent is rounded outward and a rotated placement's extent is larger than the placement, so
    ///        the extent alone would pick a decal whose corner the cursor is beside — and `26` §5 outlines the
    ///        placement rather than its extent, so the two would disagree about what was selected.
    /// note  📝 The orientation is the hit face's own flat perpendicular, rotated by the owner's composed
    ///        transform. It is not the interpolated shading perpendicular `38` derives: `78` §2 builds a drag
    ///        plane from it, and a plane that follows shading curvature slides under the manipulator.
    /// cost  🚩
    /// tag   api, nonthrowing
    ResolvedPointer Resolve(const ProjectedRay&    Projected,
                            const OctantSpace&     Subdivision,
                            const RegistrationIndex& Subsets,
                            const PlacementIndex&  Placements) const;

    /// 🧩 Resolves one display extent to the owners it encloses or touches — `74` §4's marquee.
    /// in    MinimumX          [px] the rectangle's lesser display ordinates
    /// in    MinimumY         [px]
    /// in    MaximumX       [px] its greater ones
    /// in    MaximumY      [px]
    /// in    ContainmentDeclared [-]  true registers only owners wholly inside; false registers any that overlap
    /// out   Registered            [-]  in admission order, for one registration transaction
    /// note  🔴 **One** traversal over the extent, never one per position inside it. The result is one registration
    ///        transaction in `SelectionSequence`, not one per owner — `12` §9 gates that every mutation is a
    ///        transaction, and a marquee over a thousand owners committing a thousand of them makes undo step
    ///        back one owner at a time.
    /// note  📐 A screen rectangle is a **narrower camera**, not an extent. The sub-projection's first two rows
    ///        are the camera's scaled and offset onto the rectangle's clip bounds, and `46`'s own plane
    ///        extraction is then read unamended — so the marquee frustum and the culling frustum cannot come to
    ///        disagree about a plane. Its axis-aligned bound narrows the traversal first, because `40` subdivides
    ///        extents and not frusta, and every candidate is then classified exactly against the six planes.
    /// cost  🔴
    /// tag   api, nonthrowing
    std::vector<OwnerIdentity> ResolveExtent(const CameraProjection& Camera,
                                                double                  MinimumX,
                                                double                  MinimumY,
                                                double                  MaximumX,
                                                double                  MaximumY,
                                                std::uint32_t           DisplayX,
                                                std::uint32_t           DisplayY,
                                                bool                    ContainmentDeclared,
                                                const OctantSpace&      Subdivision,
                                                const RegistrationIndex&  Subsets) const;

    std::uint32_t AcceptedCount() const;

private:

    std::size_t Located(OwnerIdentity Subject) const;

    std::vector<AcceptedSurface>  Surfaces;   // [-] - ascending by slot, then by generation
};

// 📐 Owner identity, corner ordinals and registration are Exact; the ray, the barycentric weights, the domain
//    interpolation and the frustum classification are Bounded. `00` §3's transitivity rule folds them to the
//    weaker, which is what the whole component may claim.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
