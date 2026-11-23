//============================================================================================================================================
//                                                       WORKPLANECATALOGUE.H
//============================================================================================================================================
// 🧩 The planes a workspace has: the three it always had, the ones the artist made, and which one is
//    being drawn on now.
//
// 🔴 THIS UNIT EXISTS BECAUSE A SKETCH HELD EXACTLY ONE PLANE AND FORGOT IT ON EVERY CHANGE.
//    `SketchStructure::DeclarePlane` overwrites. The workplane tool called it, so placing a second plane
//    silently RE-INTERPRETED everything already drawn as lying on the new one — the curves kept their
//    world coordinates, but every measurement, every grid line and every subsequent projection now
//    answered for a surface the geometry was never drawn on. Nothing refused. Nothing warned. The
//    drawing simply stopped meaning what it had meant, which is the "drew in the wrong place" the artist
//    reported.
//
// 🔴 A WORKPLANE IS A THING, NOT A SETTING. The shipped tool wrote a `Folder` record carrying nothing but
//    a NAME, and put the actual plane nowhere. It could not be selected, re-activated, offset, measured
//    against or removed, and reopening the workspace lost it entirely. A plane the artist can make and
//    cannot then USE is a strictly worse arrangement than no tool at all, because it also moves their
//    drawing.
//
// 📝 The arrangement here is the one every parametric sketcher settles on, for the same reason: planes are
//    a NAMED COLLECTION, one of them is ACTIVE, and a sketched curve REMEMBERS which plane it was drawn
//    on. Standing planes are entries in that collection rather than a separate mechanism, so "sketch on
//    the front plane" and "sketch on the one I just placed" are the same act. `WorkplaneStanding` already
//    knows what a plane IS and how to derive one from a click; this unit is where they live and which is
//    current. It computes no geometry of its own.
//
// ⚠️ Activating a plane never moves geometry. Curves are stored in world coordinates and stay exactly
//    where they were put; what changes is only which surface the NEXT thing is drawn on. That is the
//    property the shipped code violated, and `WorkplaneCatalogueProof` pins it.

#pragma once

#include "SlateWorkspace/Discipline/WorkplaneStanding/Api/WorkplaneStanding.h"

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

/// 🧩 Which plane in the catalogue. Index 0, 1 and 2 are always Ground, Front and Side.
struct WorkplaneName
{
    std::uint32_t IssuedIndex = 0u;

    bool Assigned() const { return IssuedIndex != 0u; }
    bool operator==(const WorkplaneName& Other) const { return IssuedIndex == Other.IssuedIndex; }
    bool operator!=(const WorkplaneName& Other) const { return IssuedIndex != Other.IssuedIndex; }
};

/// 🧩 A plane in the catalogue, with the name the artist sees.
struct CataloguedWorkplane
{
    WorkplaneName Named   = {};
    Workplane     Surface = {};
    std::string   Naming;

    /// 🧩 Whether the artist may move or remove it. The three standing planes are permanent.
    bool Removable() const { return Surface.Removable(); }
};

/// 🧩 Every plane a workspace has, and which one is being drawn on.
///
/// note 🔴 Opens holding the three standing planes with Ground active, so a workspace can be drawn in
///       before the artist has thought about planes at all. That is the same promise `WorkplaneStanding`
///       makes and this unit must not quietly withdraw it.
class WorkplaneCatalogue
{
public:
    WorkplaneCatalogue();

    /// 🧩 Adds a plane the artist made. The catalogue names it and, unless told otherwise, activates it.
    /// in    Activate  [-]  whether to start drawing on it immediately
    /// note 📝 Placing a plane and then having to select it would be two acts for one intention, so the
    ///       default is to activate. Passing false is for restoring a stored workspace, where the plane
    ///       that was active is recorded separately and must not be overridden by load order.
    WorkplaneName Declare(const Workplane& Surface, const std::string& Naming, bool Activate = true);

    /// 🧩 Starts drawing on a plane already in the catalogue.
    /// note ⚠️ Refuses an unassigned or unknown name rather than falling back to Ground. Silently drawing
    ///       on a different surface from the one asked for is the defect this unit was written to end.
    Deliver<WorkplaneName> Activate(WorkplaneName Named);

    /// 🧩 Removes a plane the artist made.
    /// note ⚠️ Refuses the three standing planes, and refuses to remove the active plane while geometry
    ///       still stands on it — the caller must move that geometry or activate another plane first.
    Deliver<WorkplaneName> Remove(WorkplaneName Named, std::uint32_t CurvesStandingOnIt);

    /// 🧩 The plane being drawn on now.
    const Workplane& Active() const;

    /// 🧩 Which plane that is.
    WorkplaneName ActiveName() const { return ActiveWorkplane; }

    /// 🧩 A plane by name, or nothing if the catalogue does not hold it.
    const CataloguedWorkplane* Resolve(WorkplaneName Named) const;

    /// 🧩 Every plane, in the order they were made.
    const std::vector<CataloguedWorkplane>& Declared() const { return HeldWorkplanes; }

    /// 🧩 How many there are.
    std::uint32_t DeclaredCount() const { return static_cast<std::uint32_t>(HeldWorkplanes.size()); }

    /// 🧩 One of the three the world always has.
    /// note 📝 Lets a caller say "the front plane" without knowing that it happens to be index 2.
    WorkplaneName StandingName(StandingWorkplane Subject) const;

private:
    std::vector<CataloguedWorkplane> HeldWorkplanes;
    WorkplaneName                    ActiveWorkplane = {};
    std::uint32_t                    IssuedCount     = 0u;
};

/// 🧩 Activates the standing plane an orthographic view looks squarely at.
///
/// in    Orientation  [-]  which way the viewport is now facing
/// in    Perspective  [-]  whether the leaf is drawing in perspective
/// out   -            [-]  true when the active plane CHANGED, so a caller can say so
///
/// note 🔴 THIS IS WHAT MAKES THE OTHER ORTHOGRAPHIC VIEWS DRAWABLE. Only Top agreed with the plane
///       being drawn on; Front and Side left Ground active, so the artist drew on a surface seen edge
///       on and the shape landed behind the pointer rather than under it.
///
/// note ⚠️ Refuses in perspective and in Isometric, both of which are square to no plane. A
///       perspective view is a free camera and the artist's chosen plane must survive orbiting.
///
/// note 📝 Only the three STANDING planes are chosen this way. A plane the artist placed themselves is
///       never overridden -- switching to a Front view while working on a placed plane leaves it
///       alone, because they asked for that surface explicitly.
bool ActivateViewedWorkplane(WorkplaneCatalogue& Catalogue,
                             ViewportOrientation Orientation,
                             bool Perspective);

/// 🧩 The name a placed plane should be given, from how many already exist.
/// note 📝 Here rather than in the host so that the name a plane gets does not depend on which product
///       made it.
std::string ResolveWorkplaneNaming(const WorkplaneCatalogue& Catalogue, WorkplaneOrigin Source);

/// 🧩 Which plane a ray from the viewer meets first, if it meets one near enough to have been aimed at.
///
/// in    RayOrigin     [-]  where the ray starts, from `ResolveViewportPlaneIntersection`'s own frame
/// in    RayDirection  [-]  which way it runs, normalised
/// in    Reach         [-]  how far from a plane's origin still counts as pointing AT it, in world units
///
/// note 🔴 THIS IS WHAT MAKES A PLANE SELECTABLE. Placing a plane is only half of what the artist asked
///       for; being able to point at one they already made and draw on it is the other half. Without
///       this, the only way to reach an existing plane is to make another one on top of it.
///
/// note ⚠️ Bounded by `Reach` rather than treating a plane as infinite. Every plane that is not edge-on
///       is met by almost every ray somewhere, so an unbounded test would always return whichever plane
///       happened to be nearest the camera and pointing at empty space would silently switch surfaces.
///
/// note 📝 Nearest along the ray wins, so a plane in front occludes one behind it, which is what the
///       artist sees.
Deliver<WorkplaneName> ResolvePointedWorkplane(const WorkplaneCatalogue& Catalogue,
                                               const SpatialPoint& RayOrigin,
                                               const SpatialDirection& RayDirection,
                                               double Reach);

}   // namespace Slate
