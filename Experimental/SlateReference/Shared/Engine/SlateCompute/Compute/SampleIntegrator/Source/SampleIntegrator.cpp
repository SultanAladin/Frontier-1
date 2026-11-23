//============================================================================================================================================
//                                                          SAMPLEINTEGRATOR.CPP
//============================================================================================================================================
// 🧩 Motion-driven reprojection, the count-derived weight, and the reset that never decays.

#include "SlateCompute/Compute/SampleIntegrator/Api/SampleIntegrator.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

const char* const AccumulationRecordingIdentity = "64-SampleIntegrator";

}   // namespace

Deliver<bool> SampleIntegrator::Declare(const RejectionSpecification& Declaring)
{
    if (!(Declaring.DepthBound > 0.0))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a depth bound of nothing refuses every history" });
    }

    if (Declaring.CountLimit == 0u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a ceiling of nothing accumulates nothing" });
    }

    Specification = Declaring;

    return Deliver<bool>::Result(true);
}

Deliver<bool> SampleIntegrator::Contribute(RenderSchedule& Schedule) const
{
    DeclaredRecording Declared;
    Declared.Identity = AccumulationRecordingIdentity;

    Declared.Produces = { SharedTarget::AccumulationSurface };

    // 📝 `MotionSurface` is declared here and by `16` §4.2 as a production, which is what orders the two. The
    //    previous rotation's `AccumulationSurface` is read too and is deliberately **not** declared: it is last
    //    rotation's residue of a target this recording itself produces, and declaring it would close a cycle in
    //    an ordering that has no notion of the rotation an coordinate came from.
    Declared.Reads = { SharedTarget::RadianceSurface,
                       SharedTarget::MotionSurface,
                       SharedTarget::DepthSurface,
                       SharedTarget::VisibilityIndex };

    Declared.Amends             = {};
    Declared.Command            = RecordingCommand::ComputeDispatch;
    Declared.CapabilityRequired = false;
    Declared.Substitution       = "";
    Declared.DisplayReferred    = false;
    Declared.AmendmentIndex   = AmendmentIndex;

    return Schedule.Contribute(Declared);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE OFFSET
//------------------------------------------------------------------------------------------------------------------------

void SampleIntegrator::OffsetOf(std::uint64_t RecordingIndex, double& OffsetX, double& OffsetY) const
{
    // 🔴 `02` §6's sequence and nothing invented here. `46` applies the same offset when it builds the
    //    projection and `82` replays it when it resolves a preview, so all three read one routine — a preview
    //    that converged to a different image than the workspace would be attributed to the preview.
    ProjectSubPixelOffset(static_cast<std::uint32_t>(RecordingIndex % SubPixelSequenceLength), OffsetX, OffsetY);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

RejectionSubject SampleIntegrator::Classify(double        ReprojectedX,
                                            double        ReprojectedY,
                                            std::uint32_t HeldOwner,
                                            std::uint32_t IncomingOwner,
                                            double        HeldDepth,
                                            double        IncomingDepth) const
{
    // 📝 Asked cheapest first, and the extent test is the one that rejects most: a camera in motion moves the
    //    whole image and the leading edge has no history at all.
    if (!HistoryCurrent)
        return RejectionSubject::OffExtent;

    if (ReprojectionOffExtent(ReprojectedX, ReprojectedY))
        return RejectionSubject::OffExtent;

    if (!ReprojectionSameOwner(HeldOwner, IncomingOwner))
        return RejectionSubject::OwnerDiffers;

    if (ReprojectionDepthRejected(HeldDepth, IncomingDepth, Specification.DepthBound))
        return RejectionSubject::DepthDiffers;

    return RejectionSubject::Accepted;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ACCUMULATION
//------------------------------------------------------------------------------------------------------------------------

void SampleIntegrator::Accumulate(AccumulatedSample& Held,
                                  const double       Incoming[3],
                                  RejectionSubject   Rejected,
                                  const double       Minimum[3],
                                  const double       Maximum[3]) const
{
    // 🔴 A refusal writes the incoming sample whole and sets the count to one. Decaying instead leaves a coloured
    //    ghost trailing every moving owner, and the ghost is more visible than the absence would be.
    if (Rejected != RejectionSubject::Accepted)
    {
        for (std::uint32_t Component = 0u; Component < 3u; ++Component)
            Held.Component[Component] = Incoming[Component];

        Held.SampleCount = 1u;

        return;
    }

    const double Weight = ProjectAccumulationWeight(Held.SampleCount, Specification.CountLimit);

    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
    {
        // 📝 The neighbourhood is widened by the declared factor before the bound is applied, so a sample that
        //    is legitimately outside its neighbours' range — a specular highlight one pixel wide — is not clipped
        //    away every rotation. A bound applied at the neighbours' exact extremes removes exactly the features
        //    the accumulation exists to resolve.
        const double Middle    = (Minimum[Component] + Maximum[Component]) * 0.5;
        const double HalfSpan  = (Maximum[Component] - Minimum[Component]) * 0.5
                               * Specification.NeighbourhoodBound;

        const double Bounded = BoundNeighbourhood(Held.Component[Component],
                                                  Middle - HalfSpan,
                                                  Middle + HalfSpan);

        Held.Component[Component] = Bounded + (Incoming[Component] - Bounded) * Weight;
    }

    Held.SampleCount = ProjectAccumulatedCount(Held.SampleCount, Specification.CountLimit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE INVALIDATIONS
//------------------------------------------------------------------------------------------------------------------------

void SampleIntegrator::Invalidate()
{
    // 🔴 `64` §8's last gate, as one line. The three moments it covers — bring-up, an extent change and a device
    //    loss — have nothing in common except that no previous result describes anything, and reading one would
    //    reproject a history addressed in pixels that no longer exist.
    HistoryCurrent = false;
}

bool SampleIntegrator::HistoryReadable() const { return HistoryCurrent; }

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORTING
//------------------------------------------------------------------------------------------------------------------------

void SampleIntegrator::DeclareRotation(std::uint32_t MinimumSampleCount,
                                       std::uint32_t MaximumSampleCount,
                                       std::uint32_t RejectedCount,
                                       std::uint32_t AccumulatedCount)
{
    Reported.MinimumSampleCount    = MinimumSampleCount;
    Reported.MaximumSampleCount = MaximumSampleCount;
    Reported.RejectedCount       = RejectedCount;
    Reported.AccumulatedCount    = AccumulatedCount;

    // 📝 A rotation that accumulated anything at all leaves a history the next one may read. Set here rather
    //    than at Declare, so that a rotation which rejected every pixel still leaves the flag standing — the
    //    histories exist, they were simply all rejected, which is a different fact from having none.
    if (AccumulatedCount != 0u)
        HistoryCurrent = true;
}

void SampleIntegrator::Report(MeasureIndex& Measured, TickPoint Sampled) const
{
    Measured.DeclareCount("64 §3 SampleIntegrator", "MinimumSampleCount", Reported.MinimumSampleCount, Sampled);
    Measured.DeclareCount("64 §3 SampleIntegrator", "MaximumSampleCount", Reported.MaximumSampleCount, Sampled);
    Measured.DeclareCount("64 §4 SampleIntegrator", "Rejected", Reported.RejectedCount, Sampled);
    Measured.DeclareCount("64 §3 SampleIntegrator", "Accumulated", Reported.AccumulatedCount, Sampled);
}

const RejectionSpecification& SampleIntegrator::Declared() const { return Specification; }
const ConvergenceMetrics&     SampleIntegrator::Metrics() const  { return Reported;      }

}   // namespace Slate
