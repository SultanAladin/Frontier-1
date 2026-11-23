//============================================================================================================================================
//                                                             DRAWERSPACE.CPP
//============================================================================================================================================
// 🧩 Grab arbitration, elastic constraint, the two snap arbitrations, and the clipped tongue.

#include "SlateUI/Interface/DrawerSpace/Api/DrawerSpace.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE FIGURES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float  ClickMargin     = 12.0f;    // [px] - extra reach around the tongue and the grip
constexpr float  GutterY    = 28.0f;    // [px] - the edge strip a withdrawn drawer is swiped in from
constexpr float  ExtentTolerance = 0.5f;     // [px] - below this the display extent did not move

/// 🧩 Accepts travel beyond a constraint at the declared elasticity.
/// cost  ✔️
double Constrain(double Coordinate, double Minimum, double Maximum, double Elasticity)
{
    if (Coordinate < Minimum)
        return Minimum - (Minimum - Coordinate) * Elasticity;

    if (Coordinate > Maximum)
        return Maximum + (Coordinate - Maximum) * Elasticity;

    return Coordinate;
}

/// 🧩 Expands an extent by the click margin on every side.
/// cost  ✔️
PlaneExtent Reach(const PlaneExtent& Exact)
{
    return PlaneExtent{ Exact.MinimumX  - ClickMargin, Exact.MinimumY - ClickMargin,
                        Exact.MaximumX   + ClickMargin, Exact.MaximumY  + ClickMargin };
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DrawerSpace::ConstructDrawerSpace(MotionIntegrator&              Integrator,
                                     const ThemeProfile& Resolved,
                                     const DrawerDeclaration&       North,
                                     const DrawerDeclaration&       South,
                                     const DisplayCondition&        Sampled)
{
    if (Sampled.Width <= 0.0f || Sampled.Height <= 0.0f)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the display extent is not positive" });

    if (Motion != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the arrangement already stands" });

    // 📝 🔴 All four registrations are attempted before any ordinal is retained. An integrator that declines
    //    the third delivers slot zero for it, and the south tongue would then drive the north drawer's
    //    across coordinate — a defect with no operand and no error.
    const Deliver<std::uint32_t> NorthY = Integrator.RegisterSpring(Resolved.Motion, 0.0);
    const Deliver<std::uint32_t> NorthTongue = Integrator.RegisterSpring(Resolved.Motion, 0.0);
    const Deliver<std::uint32_t> SouthY = Integrator.RegisterSpring(Resolved.Motion, 0.0);
    const Deliver<std::uint32_t> SouthTongue = Integrator.RegisterSpring(Resolved.Motion, 0.0);

    if (!NorthY.Resolved || !NorthTongue.Resolved ||
        !SouthY.Resolved || !SouthTongue.Resolved)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the integrator rejected a drawer spring" });
    }

    Motion       = &Integrator;
    Appearance   = &Resolved;
    Width  = Sampled.Width;
    Height = Sampled.Height;

    Slots[0]              = {};
    Slots[1]              = {};
    Slots[0].Declared     = North;
    Slots[1].Declared     = South;
    Slots[0].YSpring = NorthY.Resolve();
    Slots[0].TongueSpring = NorthTongue.Resolve();
    Slots[1].YSpring = SouthY.Resolve();
    Slots[1].TongueSpring = SouthTongue.Resolve();

    Contacts.Reset();
    GrabbedBy = DrawerBearing::BearingCount;

    Place(DrawerBearing::North, DrawerPose::Closed);
    Place(DrawerBearing::South, DrawerPose::Closed);

    return Deliver<bool>::Result(true);
}

void DrawerSpace::Reset()
{
    Motion       = nullptr;
    Appearance   = nullptr;
    Slots[0]     = {};
    Slots[1]     = {};
    Width  = 0.0f;
    Height = 0.0f;
    GrabbedBy    = DrawerBearing::BearingCount;

    Contacts.Reset();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      REARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

bool DrawerSpace::Rearrange(const DisplayCondition& Sampled)
{
    if (Motion == nullptr || Appearance == nullptr)
        return false;

    if (Sampled.Width <= 0.0f || Sampled.Height <= 0.0f)
        return false;

    // 📝 🔴 The gate the previous arrangement did not have. Re-solving is correct exactly when the extent
    //    moved; on every other tick it re-applies each spring onto its pose coordinate, drops the live grab and
    //    erases the drag one tick after it began.
    const bool Altered = std::fabs(Sampled.Width  - Width)  > ExtentTolerance
                      || std::fabs(Sampled.Height - Height) > ExtentTolerance;

    if (!Altered)
        return false;

    Width  = Sampled.Width;
    Height = Sampled.Height;

    const float Admissible = TongueAdmissible();

    for (std::uint32_t SlotIndex = 0u; SlotIndex < 2u; ++SlotIndex)
    {
        DrawerSlot&         Current = Slots[SlotIndex];
        const DrawerBearing Bearing  = static_cast<DrawerBearing>(SlotIndex);

        Current.Grabbed         = GrabSubject::Nothing;
        Current.AxisResolved   = false;
        Current.YDominant = false;
        Current.TravelY   = 0.0;
        Current.ReleaseRate    = 0.0;
        Current.CurrentCount  = 0u;
        Current.PendingCount   = 0u;

        Motion->Spring(Current.YSpring).Place(PoseY(Bearing, Current.Current));

        // 📝 The tongue's travel is clamped rather than re-derived. It is the artist's own placement along
        //    the edge and carries no fraction of the extent.
        if (Current.TongueTravel >  Admissible) Current.TongueTravel =  Admissible;
        if (Current.TongueTravel < -Admissible) Current.TongueTravel = -Admissible;

        Motion->Spring(Current.TongueSpring).Place(static_cast<double>(Current.TongueTravel));
    }

    // 📝 The contact is abandoned rather than released. A drag arbitrated against an extent that no longer
    //    exists resolves to a pose the artist never asked for.
    Contacts.Abandon();
    GrabbedBy = DrawerBearing::BearingCount;

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    POSES AND ORDINATES
//------------------------------------------------------------------------------------------------------------------------

const DrawerSpace::DrawerSlot& DrawerSpace::Slot(DrawerBearing Bearing) const
{
    return Slots[(Bearing == DrawerBearing::South) ? 1u : 0u];
}

DrawerSpace::DrawerSlot& DrawerSpace::Slot(DrawerBearing Bearing)
{
    return Slots[(Bearing == DrawerBearing::South) ? 1u : 0u];
}

double DrawerSpace::PoseY(DrawerBearing Bearing, DrawerPose Declared) const
{
    const double Extent = static_cast<double>(Height);

    if (Bearing == DrawerBearing::North)
        return (Declared == DrawerPose::Open) ? 0.0 : -Extent;

    switch (Declared)
    {
        case DrawerPose::Open: return 0.0;
        case DrawerPose::Half: return Extent * 0.5;
        default:               return Extent;
    }
}

double DrawerSpace::CurrentY(DrawerBearing Bearing) const
{
    if (Motion == nullptr)
        return PoseY(Bearing, DrawerPose::Closed);

    return Motion->Spring(Slot(Bearing).YSpring).Current;
}

DrawerPose DrawerSpace::Pose(DrawerBearing Bearing) const
{
    return Slot(Bearing).Current;
}

GrabSubject DrawerSpace::Grabbed() const
{
    return (GrabbedBy == DrawerBearing::BearingCount) ? GrabSubject::Nothing : Slot(GrabbedBy).Grabbed;
}

void DrawerSpace::Place(DrawerBearing Bearing, DrawerPose Declared)
{
    if (Motion == nullptr)
        return;

    DrawerSlot& Current = Slot(Bearing);

    if (Current.Declared.PoseCount < 3u && Declared == DrawerPose::Half)
        Declared = DrawerPose::Closed;

    Current.Current = Declared;
    Motion->Spring(Current.YSpring).Place(PoseY(Bearing, Declared));
}

void DrawerSpace::Depart(DrawerBearing Bearing, DrawerPose Declared)
{
    if (Motion == nullptr)
        return;

    DrawerSlot& Current = Slot(Bearing);

    if (Current.Declared.PoseCount < 3u && Declared == DrawerPose::Half)
        Declared = DrawerPose::Closed;

    Current.Current = Declared;

    SpringInterpolant& Travelling = Motion->Spring(Current.YSpring);
    Travelling.Target  = PoseY(Bearing, Declared);
    Travelling.Settled = false;
}

DrawerPose DrawerSpace::Opening(DrawerBearing Bearing) const
{
    const DrawerSlot& Current = Slot(Bearing);

    if (Current.Declared.PoseCount < 3u)
        return (Current.Current == DrawerPose::Open) ? DrawerPose::Closed : DrawerPose::Open;

    switch (Current.Current)
    {
        case DrawerPose::Closed: return DrawerPose::Half;
        case DrawerPose::Half:   return DrawerPose::Open;
        default:                 return DrawerPose::Closed;
    }
}

DrawerPose DrawerSpace::Closing(DrawerBearing Bearing) const
{
    const DrawerSlot& Current = Slot(Bearing);

    if (Current.Declared.PoseCount < 3u)
        return DrawerPose::Closed;

    return (Current.Current == DrawerPose::Open) ? DrawerPose::Half : DrawerPose::Closed;
}

float DrawerSpace::TongueAdmissible() const
{
    if (Appearance == nullptr)
        return 0.0f;

    const float Limit = (Width - Appearance->Measure.TongueX) * 0.5f;
    return (Limit > 0.0f) ? Limit : 0.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE SNAP ARBITRATION
//------------------------------------------------------------------------------------------------------------------------

// 📐 🔴 Transcribed literally from the source's two release handlers. Two operands, and they are not the
//    same operand in the two drawers. The north drawer compares the release's own displacement against a
//    quarter of the extent. The south drawer compares `h + offset.y` — the extent **plus** the displacement —
//    against fractions of the extent, which is the quantity its handler names `Be`.
// 📝 The nesting is the source's too. `closed` and `full` each gate an inner pair behind an outer condition.
DrawerPose DrawerSpace::Classify(DrawerBearing Bearing) const
{
    const DrawerSlot&  Current = Slot(Bearing);
    const MotionScale& Figures  = Appearance->Motion;

    const double Extent       = static_cast<double>(Height);
    const double Displacement = Current.TravelY;
    const double Rate         = Current.ReleaseRate;
    const double Near         = Extent * Figures.SnapFractionNear;
    const double Far          = Extent * Figures.SnapFractionFar;

    if (Bearing == DrawerBearing::North)
    {
        if (Current.Current == DrawerPose::Open)
            return (Displacement < -Near || Rate < -Figures.SnapRateSoft) ? DrawerPose::Closed : DrawerPose::Open;

        return (Displacement > Near || Rate > Figures.SnapRateSoft) ? DrawerPose::Open : DrawerPose::Closed;
    }

    // 📐 The source's `Be`. The south drawer rests at `h` when closed and at zero when full, so this is the
    //    coordinate the release would have left it at had nothing been constrained.
    const double Reached = Extent + Displacement;

    if (Current.Current == DrawerPose::Closed)
    {
        if (Rate < -Figures.SnapRateFirm || Reached < Far)
            return (Rate < -Figures.SnapRateHard || Reached < Near) ? DrawerPose::Open : DrawerPose::Half;

        return DrawerPose::Closed;
    }

    if (Current.Current == DrawerPose::Half)
    {
        if (Rate < -Figures.SnapRateSoft || Reached < Near) return DrawerPose::Open;
        if (Rate >  Figures.SnapRateSoft || Reached > Far)  return DrawerPose::Closed;
        return DrawerPose::Half;
    }

    if (Rate > Figures.SnapRateFirm || Reached > Near)
        return (Rate > Figures.SnapRateHard || Reached > Far) ? DrawerPose::Closed : DrawerPose::Half;

    return DrawerPose::Open;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE GRAB ARBITRATION
//------------------------------------------------------------------------------------------------------------------------

GrabSubject DrawerSpace::Contacted(DrawerBearing Bearing, float X, float Y) const
{
    const DrawerSlot& Current = Slot(Bearing);
    const bool        Visible  = !Closed(Bearing);

    // ① The grip outranks the body it sits inside, so that a contact on the pill withdraws rather than drags.
    // 📝 The grip keeps its margin on all four sides: it sits INSIDE the drawer's own body, so an inflated
    //    reach can only take pixels from the body beside it and never from the workspace beyond.
    if (Visible && Reach(Grip(Bearing)).Encloses(X, Y))
        return GrabSubject::Grip;

    // ② The tongue outranks everything, visible or not — it is the only chrome a closed drawer offers.
    // 🔴 Reached ALONG only, never across. `Reach` inflates all four sides by the click margin, which on
    //    the leading edge pushes the claim twelve pixels past the notch and INTO the workspace: a 36 px
    //    tongue claimed 48 px of depth, and the artist met an invisible band below a notch they could see
    //    the bottom of. Y the drawer's own axis the notch's drawn edge is the edge that may be
    //    pressed; along it the margin is what makes a narrow pill comfortable to hit.
    const PlaneExtent Notch   = Tongue(Bearing);
    const PlaneExtent Reached = { Notch.MinimumX - ClickMargin, Notch.MinimumY,
                                  Notch.MaximumX  + ClickMargin, Notch.MaximumY };

    if (Reached.Encloses(X, Y))
        return GrabSubject::Tongue;

    if (Visible && Body(Bearing).Encloses(X, Y))
    {
        for (std::uint32_t Index = 0u; Index < Current.CurrentCount; ++Index)
        {
            if (Current.CurrentExcluded[Index].Encloses(X, Y))
                return GrabSubject::Nothing;
        }

        return GrabSubject::Body;
    }

    // ③ The gutter — what makes a swipe from the display edge open a drawer that has no body on screen.
    if (!Visible && Gutter(Bearing).Encloses(X, Y))
        return GrabSubject::Gutter;

    return GrabSubject::Nothing;
}

bool DrawerSpace::Covers(float X, float Y, DrawerBearing& Bearing) const
{
    // 🔴 The OPEN drawer is asked first, both of them, before either withdrawn one. A raised body reaches
    //    across the display and can cover the other drawer's gutter; the drawer the artist raised is the
    //    one they mean, so it takes the contact rather than the strip hiding beneath it.
    for (std::uint32_t Index = 0u; Index < static_cast<std::uint32_t>(DrawerBearing::BearingCount); ++Index)
    {
        const DrawerBearing Asked = static_cast<DrawerBearing>(Index);

        if (Closed(Asked))
            continue;

        if (Contacted(Asked, X, Y) != GrabSubject::Nothing)
        {
            Bearing = Asked;
            return true;
        }
    }

    // 📝 Then the withdrawn ones, for their tongue and their gutter — the only chrome a closed drawer has.
    for (std::uint32_t Index = 0u; Index < static_cast<std::uint32_t>(DrawerBearing::BearingCount); ++Index)
    {
        const DrawerBearing Asked = static_cast<DrawerBearing>(Index);

        if (!Closed(Asked))
            continue;

        if (Contacted(Asked, X, Y) != GrabSubject::Nothing)
        {
            Bearing = Asked;
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE TICK
//------------------------------------------------------------------------------------------------------------------------

bool DrawerSpace::Advance(const PointerCondition& Sampled, double Elapsed, bool Available)
{
    if (Motion == nullptr || Appearance == nullptr)
        return false;

    PromoteExclusions();

    // 📝 A pointer the interface has taken for a window of its own must not also drag a drawer. A grab
    //    already live is carried to its release regardless: the window did not exist when it began.
    if (!Available && GrabbedBy == DrawerBearing::BearingCount)
    {
        Contacts.Abandon();
        return false;
    }

    const ContactTravel& Contact = Contacts.Advance(Sampled, Elapsed);

    switch (Contact.Phase)
    {
        case ContactPhase::Sampled:    return Grab(Contact);
        case ContactPhase::Travelling: return Carry(Contact);
        case ContactPhase::Released:   return Relinquish(Contact);
        default:                       break;
    }

    return GrabbedBy != DrawerBearing::BearingCount;
}

bool DrawerSpace::Grab(const ContactTravel& Contact)
{
    // 📝 Tested in reverse texture order, so the drawer drawn on top is the drawer a contact reaches first.
    const bool          SouthAbove = (Slots[1].Current == DrawerPose::Open);
    const DrawerBearing Order[2]   = { SouthAbove ? DrawerBearing::South : DrawerBearing::North,
                                       SouthAbove ? DrawerBearing::North : DrawerBearing::South };

    for (std::uint32_t Index = 0u; Index < 2u; ++Index)
    {
        const DrawerBearing Bearing = Order[Index];
        const GrabSubject   Grabbed  = Contacted(Bearing, Contact.PositionX, Contact.PositionY);

        if (Grabbed == GrabSubject::Nothing)
            continue;

        DrawerSlot& Current = Slot(Bearing);

        Current.Grabbed         = Grabbed;
        Current.AxisResolved   = (Grabbed != GrabSubject::Tongue);
        Current.YDominant = (Grabbed != GrabSubject::Tongue);
        Current.GrabbedY = CurrentY(Bearing);
        Current.GrabbedTongueY   = Current.TongueTravel;
        Current.TravelY   = 0.0;
        Current.ReleaseRate    = 0.0;

        GrabbedBy = Bearing;
        return true;
    }

    return false;
}

bool DrawerSpace::Carry(const ContactTravel& Contact)
{
    if (GrabbedBy == DrawerBearing::BearingCount)
        return false;

    DrawerSlot& Current = Slot(GrabbedBy);

    Current.TravelY = Contact.TravelY;
    Current.ReleaseRate  = Contact.RateY;

    // 📐 The tongue's axis is decided once, on the first travel that clears the tap ceiling, and by the
    //    larger of the two displacements. Deciding it per tick makes a diagonal drag alternate between
    //    sliding the notch and opening the drawer.
    if (Current.Grabbed == GrabSubject::Tongue && !Current.AxisResolved && Contact.TravelExceeded)
    {
        Current.YDominant = std::fabs(Contact.TravelY) > std::fabs(Contact.TravelX);
        Current.AxisResolved   = true;
    }

    if (Current.Grabbed == GrabSubject::Tongue && !Current.YDominant)
    {
        CarryTongue(Current, Contact);
        return true;
    }

    if (Current.Grabbed != GrabSubject::Nothing)
        CarryBody(Current, Contact);

    return true;
}

void DrawerSpace::CarryBody(DrawerSlot& Current, const ContactTravel& Contact)
{
    const bool   Northern = (GrabbedBy == DrawerBearing::North);
    const double Minimum    = Northern ? -static_cast<double>(Height) : 0.0;
    const double Maximum     = Northern ?  0.0 : static_cast<double>(Height);
    const double Dragged  = Current.GrabbedY + Contact.TravelY;

    Motion->Spring(Current.YSpring)
           .Place(Constrain(Dragged, Minimum, Maximum, Appearance->Motion.DragElasticity));
}

void DrawerSpace::CarryTongue(DrawerSlot& Current, const ContactTravel& Contact)
{
    const double Admissible = static_cast<double>(TongueAdmissible());
    const double Dragged    = static_cast<double>(Current.GrabbedTongueY) + Contact.TravelX;

    Current.TongueTravel = static_cast<float>(
        Constrain(Dragged, -Admissible, Admissible, Appearance->Motion.DragElasticity));

    Motion->Spring(Current.TongueSpring).Place(static_cast<double>(Current.TongueTravel));
}

bool DrawerSpace::Relinquish(const ContactTravel& Contact)
{
    if (GrabbedBy == DrawerBearing::BearingCount)
        return false;

    DrawerSlot&  Current   = Slot(GrabbedBy);
    const float  Admissible = TongueAdmissible();

    Current.TravelY = Contact.TravelY;
    Current.ReleaseRate  = Contact.RateY;

    const bool TongueX = (Current.Grabbed == GrabSubject::Tongue) && !Current.YDominant;

    if (Contact.TapResolved && Current.Grabbed == GrabSubject::Tongue)
    {
        // 📝 The tap the previous arrangement did not have. Its comment argued the source declares no press
        //    handler on the notch; a notch that cannot be tapped is a notch the artist reports as dead.
        Depart(GrabbedBy, Opening(GrabbedBy));
    }
    else if (Contact.TapResolved && Current.Grabbed == GrabSubject::Grip)
    {
        Depart(GrabbedBy, Closing(GrabbedBy));
    }
    else if (TongueX)
    {
        SpringInterpolant& Travelling = Motion->Spring(Current.TongueSpring);

        const float Settled = (Current.TongueTravel >  Admissible) ?  Admissible
                            : (Current.TongueTravel < -Admissible) ? -Admissible
                                                                    :  Current.TongueTravel;

        Current.TongueTravel = Settled;
        Travelling.Target     = static_cast<double>(Settled);
        Travelling.Settled    = (std::fabs(Travelling.Current - Travelling.Target) < 0.1);
    }
    else
    {
        Depart(GrabbedBy, Classify(GrabbedBy));

        // 📐 The release's own rate is injected into the spring rather than discarded. A spring departing
        //    from rest arrives visibly later than the flick that asked for it.
        Motion->Spring(Current.YSpring).Rate = Contact.RateY / 1000.0;
    }

    Loosen();
    return true;
}

void DrawerSpace::Loosen()
{
    if (GrabbedBy == DrawerBearing::BearingCount)
        return;

    DrawerSlot& Current = Slot(GrabbedBy);

    Current.Grabbed         = GrabSubject::Nothing;
    Current.AxisResolved   = false;
    Current.YDominant = false;
    Current.TravelY   = 0.0;
    Current.ReleaseRate    = 0.0;

    GrabbedBy = DrawerBearing::BearingCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE EXTENTS
//------------------------------------------------------------------------------------------------------------------------

PlaneExtent DrawerSpace::Body(DrawerBearing Bearing) const
{
    return Spanning(0.0f, static_cast<float>(CurrentY(Bearing)), Width, Height);
}

PlaneExtent DrawerSpace::Interior(DrawerBearing Bearing) const
{
    PlaneExtent Occupied = Body(Bearing);

    if (Appearance == nullptr)
        return Occupied;

    const float Strip = Appearance->Measure.GripStripHeight;

    if (Bearing == DrawerBearing::North)
        Occupied.MaximumY -= Strip;
    else
        Occupied.MinimumY += Strip;

    return Occupied;
}

PlaneExtent DrawerSpace::Tongue(DrawerBearing Bearing) const
{
    if (Appearance == nullptr)
        return {};

    const MetricScale& Measure  = Appearance->Measure;
    const PlaneExtent  Occupied = Body(Bearing);
    const float        Centre   = Width * 0.5f + Slot(Bearing).TongueTravel;
    const float        Leading  = Centre - Measure.TongueX * 0.5f;

    if (Bearing == DrawerBearing::North)
        return Spanning(Leading, Occupied.MaximumY, Measure.TongueX, Measure.TongueY);

    return Spanning(Leading, Occupied.MinimumY - Measure.TongueY,
                    Measure.TongueX, Measure.TongueY);
}

PlaneExtent DrawerSpace::Grip(DrawerBearing Bearing) const
{
    if (Appearance == nullptr)
        return {};

    const MetricScale& Measure  = Appearance->Measure;
    const PlaneExtent  Occupied = Body(Bearing);
    const bool         Northern = (Bearing == DrawerBearing::North);

    const float PillX  = Width * 0.5f - Measure.GripX * 0.5f;
    const float PillY = Northern
                           ? Occupied.MaximumY  - Measure.GripLiftNorth - Measure.GripHeight
                           : Occupied.MinimumY + (Measure.GripStripHeight - Measure.GripHeight) * 0.5f;

    return Spanning(PillX, PillY, Measure.GripX, Measure.GripHeight);
}

PlaneExtent DrawerSpace::Gutter(DrawerBearing Bearing) const
{
    if (!Closed(Bearing) || Appearance == nullptr)
        return {};

    // 🔴 The gutter spans the TONGUE and nothing more. Spanning the whole display edge put an invisible
    //    28 px band across the full width, top and bottom, that swallowed every contact near either edge —
    //    an artist reaching for a tab, a window's resize corner, or the workspace itself got a drawer
    //    swipe from a strip they could not see. The only chrome a withdrawn drawer offers is its tongue,
    //    so the only place it may claim a contact is the tongue's own run.
    // 📝 A shade wider than the tongue, by half the gutter's own depth, so a swipe that begins a pixel
    //    outside the notch still opens the drawer rather than reporting nothing.
    const PlaneExtent Notch   = Tongue(Bearing);
    const float       Reached = GutterY * 0.5f;

    const float Leading  = Notch.MinimumX - Reached;
    const float Trailing = Notch.MaximumX  + Reached;

    if (Bearing == DrawerBearing::North)
        return PlaneExtent{ Leading, 0.0f, Trailing, GutterY };

    return PlaneExtent{ Leading, Height - GutterY, Trailing, Height };
}

bool DrawerSpace::Closed(DrawerBearing Bearing) const
{
    const PlaneExtent Occupied = Body(Bearing);

    return Occupied.MaximumY <= 0.0f || Occupied.MinimumY >= Height;
}

bool DrawerSpace::Moving() const
{
    if (Motion == nullptr)
        return false;

    if (GrabbedBy != DrawerBearing::BearingCount)
        return true;

    return !Motion->Spring(Slots[0].YSpring).Settled
        || !Motion->Spring(Slots[1].YSpring).Settled
        || !Motion->Spring(Slots[0].TongueSpring).Settled
        || !Motion->Spring(Slots[1].TongueSpring).Settled;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE EXCLUSIONS
//------------------------------------------------------------------------------------------------------------------------

void DrawerSpace::Exclude(DrawerBearing Bearing, const PlaneExtent& Extent)
{
    DrawerSlot& Current = Slot(Bearing);

    if (Current.PendingCount >= ExclusionCapacity)
        return;

    Current.PendingExcluded[Current.PendingCount++] = Extent;
}

void DrawerSpace::PromoteExclusions()
{
    // 📝 A panel declares its exclusions while recording, which happens after arbitration. Promoting the
    //    previous tick's set is what lets the two run in that order without the set growing without bound.
    for (std::uint32_t SlotIndex = 0u; SlotIndex < 2u; ++SlotIndex)
    {
        DrawerSlot& Current = Slots[SlotIndex];

        for (std::uint32_t Index = 0u; Index < Current.PendingCount; ++Index)
            Current.CurrentExcluded[Index] = Current.PendingExcluded[Index];

        Current.CurrentCount = Current.PendingCount;
        Current.PendingCount  = 0u;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RECORDING
//------------------------------------------------------------------------------------------------------------------------

void DrawerSpace::Record(RecordingSurface& Surface) const
{
    if (Appearance == nullptr)
        return;

    if (Slots[1].Current == DrawerPose::Open)
    {
        RecordOne(Surface, DrawerBearing::North);
        RecordOne(Surface, DrawerBearing::South);
        return;
    }

    RecordOne(Surface, DrawerBearing::South);
    RecordOne(Surface, DrawerBearing::North);
}

void DrawerSpace::Record(RecordingSurface& Surface, DrawerBearing Bearing) const
{
    if (Appearance == nullptr)
        return;

    RecordOne(Surface, Bearing);
}

void DrawerSpace::RecordOne(RecordingSurface& Surface, DrawerBearing Bearing) const
{
    const SurfaceColour&  Colour      = Appearance->Colour;
    const MetricScale& Measure  = Appearance->Measure;
    const DrawerSlot&  Current = Slot(Bearing);
    const PlaneExtent  Occupied = Body(Bearing);
    const PlaneExtent  Tab      = Tongue(Bearing);
    const bool         Northern = (Bearing == DrawerBearing::North);
    const bool         Visible  = !Closed(Bearing);

    if (Visible)
    {
        // ① The drawer body.
        Surface.Ground(Occupied, Colour.SurfaceCurrent, 0.0f, CornerNone);

        // ② The one edge the source declares — a rule on the travelling side only.
        const float EdgeY = Northern ? Occupied.MaximumY : Occupied.MinimumY;

        Surface.Ground(PlaneExtent{ Occupied.MinimumX, EdgeY - (Northern ? 1.0f : 0.0f),
                                    Occupied.MaximumX,  EdgeY + (Northern ? 0.0f : 1.0f) },
                       Colour.EdgeQuiet, 0.0f, CornerNone);

        // ③ The grip pill, centred along, lifted from the travelling edge.
        Surface.Ground(Grip(Bearing), Colour.GripPill, Measure.GripHeight * 0.5f, CornerAll);
    }

    // ④ The tongue's clipped outline — always drawn so the notch is reachable when closed.
    const float Inset = Tab.Width() * Measure.TongueClipFraction;
    const float Minimum = Tab.MinimumX;
    const float Maximum  = Tab.MaximumX;
    const float Upper = Tab.MinimumY;
    const float Lower = Tab.MaximumY;

    const float NorthOutline[8] = { Minimum,         Upper, Maximum,          Upper,
                                    Maximum - Inset,  Lower, Minimum + Inset, Lower };
    const float SouthOutline[8] = { Minimum + Inset, Upper, Maximum - Inset,  Upper,
                                    Maximum,          Lower, Minimum,         Lower };

    Surface.Tongue(Northern ? NorthOutline : SouthOutline, 4u, Colour.SurfaceSunken);

    // ⑤ The tongue's figure and its run, measured together and centred inside the pad.
    const char* Caption = Current.Declared.Caption;
    const float RunSpan = Surface.MeasureRun(Caption, Measure.TextSmall, Measure.TrackingWide);
    const float Content = Measure.SymbolTongue + Measure.TongueGapX + RunSpan;
    const float Origin  = (Minimum + Maximum) * 0.5f - Content * 0.5f;
    const float Middle  = (Upper + Lower) * 0.5f;

    Surface.Stroke(Current.Declared.TongueSubject,
                   Spanning(Origin, Middle - Measure.SymbolTongue * 0.5f,
                            Measure.SymbolTongue, Measure.SymbolTongue),
                   Colour.ColourPrimary);

    Surface.TextRunCapitalised(Origin + Measure.SymbolTongue + Measure.TongueGapX,
                               Middle - Measure.TextSmall * 0.5f,
                               Colour.ColourPrimary, Caption, Measure.TextSmall, Measure.TrackingWide, true);
}

}   // namespace Slate
