//============================================================================================================================================
//                                                        INTERSECTIONOUTLINE.CPP
//============================================================================================================================================
// 🧩 Two indexed lookups, one interval comparison, a scalar coverage — and the refusal that stops an occluded outline being merely dimmer.

#include "SlateCompute/Compute/IntersectionOutline/Api/IntersectionOutline.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

const char* const OutlineRecordingIdentity = "26-IntersectionOutline";

// 📐 How far apart two display-space coordinates must stand before the artist reads them as two colours rather
//    than as one colour at two brightnesses. Below it, `26` §2's distinctness has to come from the dash instead.
constexpr double DistinctColourDeparture = 0.15;   // [-] - summed over the three coordinates, in the display space

}   // namespace

Deliver<bool> IntersectionOutline::Declare(const OutlineSpecification& Outlining_)
{
    if (!(Outlining_.OutlineWidth > 0.0))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "an outline width of nothing covers no pixel at any silhouette" });
    }

    if (Outlining_.OccludedDashExtent < 0.0)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a negative dash extent names no run" });
    }

    if (!Outlining_.VisibleColour.ColourDeclared() || !Outlining_.OccludedColour.ColourDeclared())
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "an outline colour declares no space" });
    }

    // 🔴 The recording is display-referred and nothing between here and the display surface compresses. A colour
    //    incoming in the working space would therefore be presented as display code without ever crossing `36`,
    //    and it reads as an outline in a plausible but wrong hue rather than as a mistake.
    if (Outlining_.VisibleColour.SpaceIdentity  != DisplaySpaceIdentity
     || Outlining_.OccludedColour.SpaceIdentity != DisplaySpaceIdentity)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "an outline colour is not a coordinate in the display space" });
    }

    const double ColourDeparture = std::fabs(Outlining_.VisibleColour.RedCoordinate   - Outlining_.OccludedColour.RedCoordinate)
                                 + std::fabs(Outlining_.VisibleColour.GreenCoordinate - Outlining_.OccludedColour.GreenCoordinate)
                                 + std::fabs(Outlining_.VisibleColour.BlueCoordinate  - Outlining_.OccludedColour.BlueCoordinate);

    // 🔴 `26` §2: the occluded rendering must be visually distinct and **not merely dimmer**. Distinctness is
    //    accepted from either the colour or the dash, and rejected only where neither supplies it — a
    //    specification satisfying it in neither has withdrawn the one thing that says which part of a selection
    //    stands behind something, and the artist meets that as a selection that looks whole when it is not.
    if (ColourDeparture < DistinctColourDeparture && !(Outlining_.OccludedDashExtent > 0.0))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the occluded outline is distinct in neither colour nor dash — `26` §2" });
    }

    Outlining       = Outlining_;
    OutlineCurrent = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> IntersectionOutline::Contribute(RenderSchedule& Schedule) const
{
    if (!OutlineCurrent)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "no outline was declared to record" });
    }

    DeclaredRecording Declared;
    Declared.Identity = OutlineRecordingIdentity;

    // 🔴 `26` §1: the outline derives from `16`'s targets and no geometry is submitted. The reads are the
    //    visibility word and the depth beside it, and there is no vertex source anywhere in this declaration.
    Declared.Reads    = { SharedTarget::VisibilityIndex, SharedTarget::DepthSurface };
    Declared.Produces = { SharedTarget::OutlineSurface };
    Declared.Amends   = { SharedTarget::DisplaySurface };

    Declared.Command            = RecordingCommand::ComputeDispatch;
    Declared.CapabilityRequired = false;
    Declared.Substitution       = "";

    // 🔴 Display-referred, unlike `66`'s own recording. This amends the display surface **after** the tone line,
    //    so the outline colour is display code already; ordered scene-referred it would be compressed with the
    //    radiance and the outline would change colour as the exposure adapted.
    Declared.DisplayReferred  = true;
    Declared.AmendmentIndex = AmendmentIndex;

    return Schedule.Contribute(Declared);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ENROLMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> IntersectionOutline::ClassifyRegistration(VisibilityWord                  Written,
                                                     const VisibilityIndex&          Visibility,
                                                     const PartitionResolutionIndex& Resolutions,
                                                     const RegistrationIndex&          Registrations) const
{
    // ① The pixel resolves through `16`, which performs the two indexed lookups and refuses an unoccupied pixel.
    //    Nothing here reconstructs an owner from a partition ordinal — that relation exists only in `42`.
    const Deliver<ResolvedPartition> Resolved = Visibility.Resolve(Written, Resolutions);

    if (!Resolved.Resolved)
    {
        return Deliver<bool>::Refuse(Resolved.Error);
    }

    // ② Registration, answered by `12`'s interval comparison over its compressed runs. Held as a call rather than as
    //    a structure beside it, so the outline and the document cannot disagree about what is selected.
    const bool Registered = Registrations.Registered(Resolved.Resolve().Owner, SubsetSubject::Selection);

    return Deliver<bool>::Result(Registered);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE COVERAGE
//------------------------------------------------------------------------------------------------------------------------

double IntersectionOutline::ProjectCoverage(double DivergenceExtent) const
{
    if (DivergenceExtent < 0.0 || DivergenceExtent >= Outlining.OutlineWidth)
    {
        return 0.0;
    }

    // 📐 ③ A pixel is an outline pixel when a neighbour within the width disagrees with it about registration. The
    //    coverage is unity at the divergence itself and falls linearly to nothing at the declared width, so the
    //    silhouette carries a gradient rather than the one-pixel staircase a binary decision leaves.
    return 1.0 - DivergenceExtent / Outlining.OutlineWidth;
}

bool IntersectionOutline::ClassifyOcclusion(double OutlineDepth, double RecordedDepth) const
{
    return OutlineDepth < RecordedDepth;
}

bool IntersectionOutline::DashCurrent(double XCoordinate, double YCoordinate) const
{
    if (!(Outlining.OccludedDashExtent > 0.0))
    {
        return true;
    }

    const double Period   = Outlining.OccludedDashExtent * 2.0;
    const double Position = std::fmod(XCoordinate + YCoordinate, Period);

    return (Position < 0.0 ? Position + Period : Position) < Outlining.OccludedDashExtent;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE COLOUR
//------------------------------------------------------------------------------------------------------------------------

Deliver<ColourSpecification> IntersectionOutline::OutlineColour(bool Occluded) const
{
    if (!OutlineCurrent)
    {
        return Deliver<ColourSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "no outline was declared to draw in" });
    }

    // ⑤ Delivered in the display space and recorded as it stands. `26` §6: never tone-mapped, never reflected,
    //    never accumulated — the whole reason the recording is ordered after `66` rather than among its inputs.
    return Deliver<ColourSpecification>::Result(Occluded ? Outlining.OccludedColour : Outlining.VisibleColour);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORTING
//------------------------------------------------------------------------------------------------------------------------

void IntersectionOutline::Report(MeasureIndex& Measured, TickPoint Sampled) const
{
    Measured.DeclareMagnitude("26 §4 IntersectionOutline", "OutlineWidth", Outlining.OutlineWidth, Sampled);
    Measured.DeclareMagnitude("26 §2 IntersectionOutline", "DashExtent", Outlining.OccludedDashExtent, Sampled);
}

const OutlineSpecification& IntersectionOutline::Outline() const { return Outlining; }

}   // namespace Slate
