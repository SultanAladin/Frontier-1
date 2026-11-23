//============================================================================================================================================
//                                                        OVERLAYPROJECTION.CPP
//============================================================================================================================================
// 🧩 Two declarations that differ in exactly one behaviour, presence read straight out of `76`, and nothing that could ever be picked.

#include "SlateCompute/Compute/OverlayProjection/Api/OverlayProjection.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

const char* const DepthTestedIdentity = "80-OverlayProjection-DepthTested";
const char* const DepthFreeIdentity   = "80-OverlayProjection-DepthFree";
const char* const OverlayOrigin       = "80 §3 OverlayProjection";

constexpr bool OverlayDeclarable(OverlaySubject Current)
{
    return static_cast<std::uint32_t>(Current) < static_cast<std::uint32_t>(OverlaySubject::OverlayCount);
}

}   // namespace

Deliver<bool> OverlayProjection::Declare(OverlaySubject Current, const OverlaySpecification& Declaring)
{
    if (!OverlayDeclarable(Current))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the closed count is not an overlay" });
    }

    if (!Declaring.OverlayColour.ColourDeclared())
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "an overlay colour declares no space" });
    }

    // 🔴 `80` §2: both recordings run after `66` and nothing between here and the display surface compresses. A
    //    colour incoming in the working space would be presented as display code without ever crossing `36`, and it
    //    reads as an overlay in a plausible but wrong hue rather than as a mistake.
    if (Declaring.OverlayColour.SpaceIdentity != DisplaySpaceIdentity)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "an overlay colour is not a coordinate in the display space" });
    }

    if (!(Declaring.LineExtent > 0.0))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a line extent of nothing draws the overlay at no pixel" });
    }

    // 🔴 Rejected rather than ignored. A depth-free recording tests nothing, so an offset declared against it has no
    //    comparison to displace; ignored silently, the caller reads it as an offset that was too small and raises it
    //    until something the offset does reach breaks instead.
    if (DepthOfOverlay(Current) == DepthSubject::DepthFree && Declaring.DepthOffset != 0.0)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a depth-free overlay tests no depth to offset — `80` §1" });
    }

    const std::size_t Index = static_cast<std::size_t>(Current);

    Declared[Index]            = Declaring;
    DeclarationCurrent[Index] = true;
    OverlayDeclared              = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RECORDINGS
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OverlayProjection::Contribute(RenderSchedule& Schedule) const
{
    if (!OverlayDeclared)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "no overlay was declared to record" });
    }

    // ① `08` §3 ⑩ — depth-tested. It amends `DepthSurface` as well as `DisplaySurface`, because depth-tested
    //    overlays occlude one another and a guide behind the ground lattice must be drawn behind it.
    DeclaredRecording Tested;
    Tested.Identity = DepthTestedIdentity;
    Tested.Reads    = { SharedTarget::DepthSurface };
    Tested.Produces = {};
    Tested.Amends   = { SharedTarget::DepthSurface, SharedTarget::DisplaySurface };

    // 🔴 `80` §4: neither recording writes `VisibilityIndex`, `OccupancySurface` or `MotionSurface`. Written there,
    //    the ground lattice would be picked by `74`, outlined by `26` when the artist selected it, and shaded by
    //    `18` as though it were a surface — three defects from one line.
    Tested.Command            = RecordingCommand::GraphicsRecording;
    Tested.CapabilityRequired = false;
    Tested.Substitution       = "";
    Tested.DisplayReferred    = true;
    Tested.AmendmentIndex   = DepthTestedIndex;

    const Deliver<bool> TestedContributed = Schedule.Contribute(Tested);

    if (!TestedContributed.Resolved)
    {
        return TestedContributed;
    }

    // ② `08` §3 ⑪ — depth-free. It reads no depth and amends none: a manipulator that respected depth would
    //    disappear inside the object it manipulates, which is `80` §1's second half.
    DeclaredRecording Free;
    Free.Identity = DepthFreeIdentity;
    Free.Reads    = {};
    Free.Produces = {};
    Free.Amends   = { SharedTarget::DisplaySurface };

    Free.Command            = RecordingCommand::GraphicsRecording;
    Free.CapabilityRequired = false;
    Free.Substitution       = "";
    Free.DisplayReferred    = true;
    Free.AmendmentIndex   = DepthFreeIndex;

    return Schedule.Contribute(Free);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE PRESENCE
//------------------------------------------------------------------------------------------------------------------------

bool OverlayProjection::OverlayActive(const ToolSequence& Tooling, OverlaySubject Current) const
{
    if (!OverlayDeclarable(Current))
    {
        return false;
    }

    // 🔴 `80` §3's closing line: which overlays are present is held in `76` and is read here on the rotation it is
    //    read on. Nothing is copied — a held copy disagrees with the toggle the artist has just used, and the overlay
    //    then appears in one recording and not the other for as long as the copies differ.
    return DeclarationCurrent[static_cast<std::size_t>(Current)]
        && Tooling.OverlayActive(Current);
}

bool OverlayProjection::RecordingOccupied(const ToolSequence& Tooling, DepthSubject Behaviour) const
{
    for (std::uint32_t Index = 0u;
         Index < static_cast<std::uint32_t>(OverlaySubject::OverlayCount);
         ++Index)
    {
        const OverlaySubject Current = static_cast<OverlaySubject>(Index);

        if (DepthOfOverlay(Current) == Behaviour && OverlayActive(Tooling, Current))
        {
            return true;
        }
    }

    return false;
}

Deliver<const OverlaySpecification*> OverlayProjection::Specification(OverlaySubject Current) const
{
    if (!OverlayDeclarable(Current))
    {
        return Deliver<const OverlaySpecification*>::Refuse(
            { RefusalReason::ContentUnsupported, "the closed count is not an overlay" });
    }

    const std::size_t Index = static_cast<std::size_t>(Current);

    if (!DeclarationCurrent[Index])
    {
        return Deliver<const OverlaySpecification*>::Refuse(
            { RefusalReason::ContentUnsupported, "that overlay was never declared" });
    }

    return Deliver<const OverlaySpecification*>::Result(&Declared[Index]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORTING
//------------------------------------------------------------------------------------------------------------------------

void OverlayProjection::Report(const ToolSequence& Tooling, MeasureIndex& Measured, TickPoint Sampled) const
{
    std::uint64_t TestedCount = 0u;
    std::uint64_t FreeCount   = 0u;

    for (std::uint32_t Index = 0u;
         Index < static_cast<std::uint32_t>(OverlaySubject::OverlayCount);
         ++Index)
    {
        const OverlaySubject Current = static_cast<OverlaySubject>(Index);

        if (!OverlayActive(Tooling, Current))
        {
            continue;
        }

        if (DepthOfOverlay(Current) == DepthSubject::DepthTested)
        {
            ++TestedCount;
        }
        else
        {
            ++FreeCount;
        }
    }

    // 📝 The counts overwrite and nothing is appended. An overlay that is drawn is the component working, and a
    //    report each rotation would bury the one obligation the artist did not expect — `86` §2.
    Measured.DeclareCount(OverlayOrigin, "DepthTestedOverlays", TestedCount, Sampled);
    Measured.DeclareCount(OverlayOrigin, "DepthFreeOverlays",   FreeCount,   Sampled);
}

}   // namespace Slate
