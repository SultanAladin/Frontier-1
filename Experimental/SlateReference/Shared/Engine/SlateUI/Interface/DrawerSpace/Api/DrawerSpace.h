//============================================================================================================================================
//                                                              DRAWERSPACE.H
//============================================================================================================================================
// 🧩 Two drawers over one display extent — their grab arbitration, their snap arbitration, their tongues and their interiors.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/GestureSequence/Api/GestureSequence.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    BEARING AND POSE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which edge a drawer is anchored to and travels from.
/// tag   guarantee
enum class DrawerBearing : std::uint32_t
{
    North        = 0u,   // [-] - enters from the upper edge; the Control Centre
    South        = 1u,   // [-] - enters from the lower edge; the Asset Browser
    BearingCount = 2u    // [-] - the closed count, never a bearing
};

/// 🧩 Where a drawer rests when nothing is dragging it.
/// note  🔴 The north drawer takes `Closed` and `Open` only. The south drawer takes all three, and its
///       `Open` is the source's "full": the drawer covering the whole display extent.
/// tag   guarantee
enum class DrawerPose : std::uint32_t
{
    Closed    = 0u,   // [-] - wholly outside the display extent
    Half      = 1u,   // [-] - half the extent; south drawer only
    Open      = 2u,   // [-] - covering the whole extent
    PoseCount = 3u    // [-] - the closed count, never a pose
};

/// 🧩 What one contact grabbed when it arrived.
/// note  🔴 The subject is decided once, at arrival, and never re-decided while the contact stands. A
///       subject re-derived every tick changes underneath a drag the moment the extent it was tested
///       against moves — which is the drag moving it.
/// tag   guarantee
enum class GrabSubject : std::uint32_t
{
    Nothing      = 0u,   // [-] - the contact grabbed no drawer
    Body         = 1u,   // [-] - the drawer's own extent, less whatever a panel withheld
    Tongue       = 2u,   // [-] - the trapezoidal notch; resolves to one axis on first travel
    Grip         = 3u,   // [-] - the pill at the travelling edge; drags across, taps to withdraw
    Gutter       = 4u,   // [-] - the edge strip a withdrawn drawer is swiped in from
    SubjectCount = 5u    // [-] - the closed count, never a subject
};

/// 🧩 What one drawer is declared with at bring-up.
/// tag   guarantee, nonallocating, nonthrowing
struct DrawerDeclaration
{
    const char*    Caption       = "";                           // [-] - the tongue's run, in small capitals
    SymbolSubject  TongueSubject = SymbolSubject::FolderClosed;   // [-] - the figure left of the caption
    std::uint32_t  PoseCount     = 2u;                            // [-] - two or three; anything else is two
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The two drawers, the contact arbitration between them, and the extents their panels record inside.
/// note  🔴 📐 The snap arbitration is evaluated on the **drag displacement since contact arrived**, never on
///       the drawer's absolute coordinate. Against an absolute coordinate the closed drawer's `y > h/4` can
///       never hold and the drawer never opens by drag at all.
/// note  🔴 `Rearrange` re-solves **only when the display extent actually moved**. Called unconditionally it
///       re-applies every spring onto its pose coordinate and releases the live grab, which erases a drag one
///       tick after it began and teleports every departing spring onto its target.
/// note  ⚠️ This component raises no mark of its own. `ViewportSequence` reads `Moving` and marks.
/// tag   owning
class DrawerSpace
{
public:

    static constexpr std::uint32_t ExclusionCapacity = 256u;   // [-] - controls one drawer may withhold per tick

    DrawerSpace()                              = default;
    DrawerSpace(const DrawerSpace&)            = delete;
    DrawerSpace& operator=(const DrawerSpace&) = delete;
    ~DrawerSpace()                             = default;

    /// 🧩 Registers four springs and applies both drawers closed at the arrived display extent.
    /// in    Motion      [-]  the one integrator; borrowed and outlives this component
    /// in    Appearance  [-]  already resolved against the display scale; borrowed and outlives this
    /// in    North       [-]  what the upper drawer's tongue carries
    /// in    South       [-]  what the lower drawer's tongue carries
    /// in    Sampled     [-]  the display extent this tick reported
    /// out   Result     [-]  refuses with ContentUnsupported for a display extent at or below zero, and
    ///                        with ExtentExhausted when the integrator declines a spring
    /// err   rejected in full; a partial registration would leave one drawer driving the other's coordinate
    /// post  both drawers stand Closed and settled; nothing moves until a contact arrives
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> ConstructDrawerSpace(MotionIntegrator&              Motion,
                            const ThemeProfile& Appearance,
                            const DrawerDeclaration&       North,
                            const DrawerDeclaration&       South,
                            const DisplayCondition&        Sampled);

    /// 🧩 Re-solves both drawers against a new display extent, holding each pose.
    /// in    Sampled  [-]  this tick's display condition
    /// out   Altered  [-]  true when the extent moved and the arrangement was re-solved
    /// note  🔴 Returns immediately when the extent is unchanged. Every coordinate is a fraction of the
    ///       extent, so re-solving is correct on a change and destructive on every other tick.
    /// post  on a change: the live contact is abandoned, exclusions are dropped, springs are re-applied
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Rearrange(const DisplayCondition& Sampled);

    /// 🧩 Advances one tick of contact arbitration — seize, carry, release, snap, tongue travel.
    /// in    Sampled    [-]   what `RecordingSurface::Pointer` sampled this tick
    /// in    Elapsed    [ms]  what the same tick's display condition measured
    /// in    Available  [-]   false when the interface has taken the pointer for a window of its own
    /// out   Taken      [-]   true when the drawer chrome holds the pointer; the interior must then ignore it
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Advance(const PointerCondition& Sampled, double Elapsed, bool Available = true);

    /// 🧩 Records both drawers — bodies, edges, grips, tongues and tongue runs.
    /// note  🔴 The south drawer is recorded last while it stands Open, and first otherwise.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Record(RecordingSurface& Surface) const;

    /// 🧩 Records one drawer alone, so a caller may interleave a panel between the two.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Record(RecordingSurface& Surface, DrawerBearing Bearing) const;

    /// 🧩 Withholds one extent inside a drawer from initiating an across drag, for this tick.
    /// in    Extent  [px] in display ordinates, as the panel recorded it
    /// note  ⚠️ Declared per tick. The pending set is promoted at the next Advance and the standing set is
    ///       what arbitration tests against, so a panel declares its extents every tick or loses them.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Exclude(DrawerBearing Bearing, const PlaneExtent& Extent);

    /// 🧩 The pose one drawer is settled at, or heading toward while a spring is live.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    DrawerPose Pose(DrawerBearing Bearing) const;

    /// 🧩 What the live contact grabbed, if anything.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    GrabSubject Grabbed() const;

    /// 🧩 Places one drawer at a pose immediately, discarding any motion.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Place(DrawerBearing Bearing, DrawerPose Declared);

    /// 🧩 Starts one drawer travelling toward a pose under its spring — what a tongue tap does.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Depart(DrawerBearing Bearing, DrawerPose Declared);

    /// 🧩 The whole extent one drawer's body occupies, including the region its grip sits in.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PlaneExtent Body(DrawerBearing Bearing) const;

    /// 🧩 The extent a panel records inside — the body less the grip strip at its travelling edge.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PlaneExtent Interior(DrawerBearing Bearing) const;

    /// 🧩 The tongue's extent, for a caller that wants to place something against it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PlaneExtent Tongue(DrawerBearing Bearing) const;

    /// 🧩 The grip pill's extent at the travelling edge.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PlaneExtent Grip(DrawerBearing Bearing) const;

    /// 🧩 The edge strip a withdrawn drawer is swiped in from.
    /// out   Gutter  [px] empty while the drawer is not withdrawn
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PlaneExtent Gutter(DrawerBearing Bearing) const;

    /// 🧩 Whether one drawer's body lies wholly outside the display extent.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Closed(DrawerBearing Bearing) const;

    /// 🧩 Whether either drawer is being dragged or is still travelling under its spring.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Moving() const;

    /// 🧩 Whether the drawers claim a contact at one point, and which bearing claims it.
    /// in    X    [px]  the pointer, in display pixels
    /// in    Y   [px]
    /// out   Target  [-]   true when either drawer would take a contact there
    /// out   Bearing  [-]   which one claims it; untouched when nothing does
    /// note  🔴 Answered WITHOUT the interface's capture flag, because it is what decides that flag rather
    ///        than what obeys it. A drawer drawn over a workspace was previously rejected every contact:
    ///        the workspace window sets `WantCaptureMouse`, the drawers were gated on its negation, so
    ///        the handle the artist could see was the one thing they could not press.
    /// note  ⚠️ An OPEN drawer outranks a withdrawn one. Both tongues sit at opposite display edges and
    ///        cannot overlap, but a raised body can reach across the other's gutter — and the drawer the
    ///        artist raised is the one they mean.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Covers(float X, float Y, DrawerBearing& Bearing) const;

    /// 🧩 Returns the arrangement to its constructed condition and forgets both borrowed references.
    /// note  🔴 Springs already registered in the integrator are **not** withdrawn. A re-Construct against the
    ///       same integrator registers four more; reclaim the integrator alongside this.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reset();

private:

    /// 🧩 One drawer's own condition. Its coordinate lives in the integrator; everything else lives here.
    /// tag   nonallocating, nonthrowing
    struct DrawerSlot
    {
        DrawerDeclaration  Declared        = {};                    // [-]    - as supplied, never re-read
        std::uint32_t      YSpring    = 0u;                    // [-]    - ordinal into the integrator
        std::uint32_t      TongueSpring    = 0u;                    // [-]    - the tongue's elastic release
        DrawerPose         Current        = DrawerPose::Closed;    // [-]    - settled, or being travelled to
        GrabSubject        Grabbed          = GrabSubject::Nothing;  // [-]    - decided once, at arrival
        float              TongueTravel    = 0.0f;                  // [px]   - signed, from the along centre
        float              GrabbedTongueY    = 0.0f;                  // [px]   - where the tongue was at grab
        double             GrabbedY  = 0.0;                   // [px]   - the across coordinate at grab
        double             TravelY    = 0.0;                   // [px]   - displacement since arrival
        double             ReleaseRate     = 0.0;                   // [px/s] - smoothed, signed
        bool               AxisResolved    = false;                 // [-]    - the tongue's axis is decided
        bool               YDominant  = false;                 // [-]    - and it is the across one
        PlaneExtent        CurrentExcluded[ExclusionCapacity] = {};// [px]  - what arbitration tests against
        PlaneExtent        PendingExcluded[ExclusionCapacity]  = {};// [px]  - what this tick declared
        std::uint32_t      CurrentCount   = 0u;                    // [-]
        std::uint32_t      PendingCount    = 0u;                    // [-]
    };

    /// 🧩 Which slot one bearing names; anything outside the two bearings names the north slot.
    const DrawerSlot& Slot(DrawerBearing Bearing) const;
    DrawerSlot&       Slot(DrawerBearing Bearing);

    /// 🧩 The across coordinate one pose rests at, at the standing extent.
    double PoseY(DrawerBearing Bearing, DrawerPose Declared) const;

    /// 🧩 The across coordinate one drawer stands at now, read from its spring.
    double CurrentY(DrawerBearing Bearing) const;

    /// 🧩 The pose a release resolves to, from the drawer's own arbitration.
    DrawerPose Classify(DrawerBearing Bearing) const;

    /// 🧩 The pose a tongue tap advances to, and the pose a grip tap withdraws to.
    DrawerPose Opening(DrawerBearing Bearing) const;
    DrawerPose Closing(DrawerBearing Bearing) const;

    /// 🧩 What a contact at this position would seize on this drawer.
    GrabSubject Contacted(DrawerBearing Bearing, float X, float Y) const;


    /// 🧩 The three carriage paths and the two resolutions.
    bool Grab(const ContactTravel& Contact);
    bool Carry(const ContactTravel& Contact);
    void CarryBody(DrawerSlot& Current, const ContactTravel& Contact);
    void CarryTongue(DrawerSlot& Current, const ContactTravel& Contact);
    bool Relinquish(const ContactTravel& Contact);
    void Loosen();

    /// 🧩 The tongue's admissible travel at the standing extent.
    float TongueAdmissible() const;

    /// 🧩 Promotes the pending exclusion set and empties it for the tick about to be declared.
    void PromoteExclusions();

    /// 🧩 Records one drawer in full.
    void RecordOne(RecordingSurface& Surface, DrawerBearing Bearing) const;

    MotionIntegrator*              Motion       = nullptr;   // [-]  - borrowed; never owned
    const ThemeProfile* Appearance   = nullptr;   // [-]  - borrowed; never owned
    GestureSequence                Contacts     = {};        // [-]  - one contact, resolved per tick
    DrawerSlot                     Slots[2]     = {};        // [-]  - north, then south
    float                          Width  = 0.0f;      // [px] - the display's drawable extent
    float                          Height = 0.0f;      // [px]
    DrawerBearing                  GrabbedBy    = DrawerBearing::BearingCount;   // [-] - which drawer holds
                                                                                 //       the pointer, if any
};

}   // namespace Slate
