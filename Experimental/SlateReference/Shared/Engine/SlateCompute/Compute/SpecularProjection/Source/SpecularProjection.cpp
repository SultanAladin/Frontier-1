//============================================================================================================================================
//                                                         SPECULARPROJECTION.CPP
//============================================================================================================================================
// 🧩 The half-extent claim, the amending recording ordered after `62`, and the composite that cancels to nothing on every failure.

#include "SlateCompute/Compute/SpecularProjection/Api/SpecularProjection.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

const char* const ReflectionRecordingIdentity = "30-SpecularProjection";

}   // namespace

Deliver<bool> SpecularProjection::Declare(const ReflectionSpecification& Declaring)
{
    if (Declaring.MarchLimit == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a march of no step resolves nothing" });

    if (!(Declaring.ThicknessBound > 0.0))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a thickness of nothing accepts no crossing at all" });
    }

    if (Declaring.RoughnessLimit < 0.0 || Declaring.RoughnessLimit > 1.0)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the roughness ceiling lies outside the channel's own interval" });
    }

    // ⚠️ Rejected above two rather than accepted as a quality setting — `08` §2 claims `ReflectionSurface` at half
    //    extent and nowhere else, and a third of the extent would declare it in two places that can disagree.
    if (Declaring.ExtentDivisor != 2u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "`08` §2 claims the target at half extent and nowhere else" });
    }

    Specification = Declaring;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SpecularProjection::Contribute(RenderSchedule& Schedule) const
{
    DeclaredRecording Declared;
    Declared.Identity = ReflectionRecordingIdentity;

    // 🔴 `ReflectionSurface` carries `18`'s **pre-added** contribution in its components and the resolved weight
    //    in its fourth. That is what makes `30` §1's composite expressible: a target carrying the trace result
    //    would leave the resolve nothing to subtract, and the ambient specular would be counted twice wherever
    //    the trace succeeded.
    Declared.Produces = { SharedTarget::ReflectionSurface };

    // 📝 `DepthSurface` is marched against and `VisibilityIndex` supplies the orientation the reflection is taken
    //    about. `RadianceSurface` is read at the crossing and amended at the pixel, so it appears in the
    //    amendment list rather than in the reads — an amendment is a read-modify-write and listing it twice
    //    would make the ordering claim a dependency the target's own producer already satisfies.
    Declared.Reads  = { SharedTarget::DepthSurface, SharedTarget::VisibilityIndex };
    Declared.Amends = { SharedTarget::RadianceSurface };

    Declared.Command            = RecordingCommand::ComputeDispatch;
    Declared.CapabilityRequired = false;
    Declared.Substitution       = "";
    Declared.DisplayReferred    = false;
    Declared.AmendmentIndex   = AmendmentIndex;

    return Schedule.Contribute(Declared);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE EXTENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SpecularProjection::Resolve(std::uint32_t  DisplayX,
                                          std::uint32_t  DisplayY,
                                          std::uint32_t& ResolvedX,
                                          std::uint32_t& ResolvedY) const
{
    if (DisplayX == 0u || DisplayY == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a display extent of nothing" });

    ResolvedX  = (DisplayX  + Specification.ExtentDivisor - 1u) / Specification.ExtentDivisor;
    ResolvedY = (DisplayY + Specification.ExtentDivisor - 1u) / Specification.ExtentDivisor;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE COMPOSITE
//------------------------------------------------------------------------------------------------------------------------

void SpecularProjection::Compose(const double            Current[3],
                                 const double            PreAdded[3],
                                 const TracedReflection& Traced,
                                 double                  Resolved[3]) const
{
    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
    {
        Resolved[Component] = ResolveExactComposite(Current[Component],
                                                    PreAdded[Component],
                                                    Traced.Component[Component],
                                                    Traced.Weight);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORTING
//------------------------------------------------------------------------------------------------------------------------

void SpecularProjection::DeclareRotation(std::uint32_t TracedCount,
                                         std::uint32_t ResolvedCount,
                                         std::uint32_t SkippedCount)
{
    Reported.TracedCount   = TracedCount;
    Reported.ResolvedCount = ResolvedCount;
    Reported.SkippedCount  = SkippedCount;
}

void SpecularProjection::Report(MeasureIndex& Measured, TickPoint Sampled) const
{
    // 🔴 Measures only. `30` is absent from `86` §4's register by design: every one of `30` §3's four failures is
    //    the mechanism operating as declared, and appending one per failed pixel would leave the register full of
    //    reports about a feature that is working.
    Measured.DeclareCount("30 §2 SpecularProjection", "Traced", Reported.TracedCount, Sampled);
    Measured.DeclareCount("30 §2 SpecularProjection", "Resolved", Reported.ResolvedCount, Sampled);
    Measured.DeclareCount("30 §4 SpecularProjection", "Skipped", Reported.SkippedCount, Sampled);
    Measured.DeclareCount("30 §2 SpecularProjection", "StepsTaken", Reported.StepsTaken, Sampled);
}

const ReflectionSpecification& SpecularProjection::Declared() const { return Specification; }
const ReflectionMetrics&       SpecularProjection::Metrics() const  { return Reported;      }

}   // namespace Slate
