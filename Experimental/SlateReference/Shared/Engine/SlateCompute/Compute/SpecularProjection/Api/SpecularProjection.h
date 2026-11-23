//============================================================================================================================================
//                                                          SPECULARPROJECTION.H
//============================================================================================================================================
// 🧩 `30` — screen-space reflection over depth already resolved and radiance already shaded, composed so that nothing is counted twice.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Shared/ReflectionProjection.slang.h"
#include "SlateCompute/Compute/TransmissionSequence/Api/TransmissionSequence.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT IS DECLARED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the trace is bounded by.
/// note  🚧 Every figure below is one of `30` §7's open rows and each blocks tuning alone. They are declared
///        here rather than in `Foundation/` because no second unit reads one — `00` §2's rule, applied to a
///        number that is a measurement waiting to happen rather than an agreement.
/// tag   nonallocating, nonthrowing
struct ReflectionSpecification
{
    std::uint32_t  MarchLimit      = 48u;    // [-] - steps before the ray gives up
    std::uint32_t  RefineCount       = 6u;     // [-] - binary-search steps refining a crossing
    double         ThicknessBound    = 0.004;  // [-] - in reversed depth; a crossing deeper is not a hit
    double         RoughnessLimit  = 0.55;   // [-] - above this the trace is skipped entirely
    std::uint32_t  ExtentDivisor     = 2u;     // [-] - `08` §2's half extent
    bool           JitterDeclared    = true;   // [-] - `30` §7's row; `64` owns the resolve either way
};

/// 🧩 One traced result and the weight it carries into the composite.
/// note  🔴 `Weight` is nothing on **every** failure — off the extent, past the ceiling, behind a surface, or
///        pointing away from the camera. `30` §3's table is four rows and one result, which is what lets the
///        march terminate anywhere it likes.
/// tag   nonallocating, nonthrowing
struct TracedReflection
{
    double         Component[3]  = { 0.0, 0.0, 0.0 };   // [-] - `RadianceSurface` sampled at the crossing
    double         Weight        = 0.0;                 // [-] - how much of the specular the trace resolved
    std::uint32_t  StepsTaken    = 0u;                  // [-] - for `86`; never read by the composite
    bool           Resolved      = false;               // [-] - a crossing was refined at all
};

/// 🧩 What `30` reports through `86`.
/// note  🔴 Every row overwrites. `86` §5 rules a failed trace ordinary operation — it is the mechanism working
///        as `30` §3 designed it, and reporting one would mean the register is never quiet.
/// tag   nonallocating, nonthrowing
struct ReflectionMetrics
{
    std::uint32_t  TracedCount    = 0u;   // [-] - pixels the trace ran at
    std::uint32_t  ResolvedCount  = 0u;   // [-] - pixels that resolved a crossing
    std::uint32_t  SkippedCount   = 0u;   // [-] - pixels above the roughness ceiling
    std::uint64_t  StepsTaken     = 0u;   // [-] - march steps across the rotation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `30` — the amending recording, the march, and the composite that subtracts before it adds.
/// note  🔴 `30` §5: this is an **amending** recording and not a producing one. `18` produces `RadianceSurface`;
///        `62` and `30` amend it in that order, and `08` §2's amendment list is what makes that legal and
///        ordered. `08` §6 previously gated "no shared target is produced by two recordings" while this section
///        wrote back into a target `18` produced — recorded as `00` §10 conflict 26 and closed by the ordinal.
/// note  🔴 `30` §5: the trace reads `RadianceSurface` at the hit, so it reads `62`'s amendment — a reflection of
///        a transmissive owner shows that owner. It reads nothing display-referred, so a selection outline
///        appearing inside a mirror is impossible by ordering rather than by a test.
/// note  🔴 No geometry is submitted and no second visibility resolution occurs — `30` §6. Everything the trace
///        needs was already resolved by `16` and shaded by `18`.
/// tag   owning
class SpecularProjection
{
public:

    // 📝 Above `62`'s resolution ordinal, so the schedule places this after it. `30` §5 requires the ordering
    //    and `08` §2's amendment list declares it; the ordinal is that declaration expressed as a number.
    static constexpr std::uint32_t AmendmentIndex = 30u;   // [-] - `08` §3 ⑥

    /// 🧩 Declares what the trace is bounded by.
    /// out   Result  [-]  refuses with ContentUnsupported for a march ceiling of nothing, a non-positive
    ///                     thickness, and a divisor that is not two
    /// note  ⚠️ The divisor is rejected above two rather than accepted as a quality setting, for the reason
    ///        `60`'s ambient term refuses one: `08` §2 claims `ReflectionSurface` at half extent, and accepting
    ///        a third would declare the extent in two places that could disagree.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Declare(const ReflectionSpecification& Declaring);

    /// 🧩 Contributes `08` §3 ⑥'s recording.
    /// out   Result  [-]  refuses with whatever the schedule rejected
    /// note  📝 Produces `ReflectionSurface` and amends `RadianceSurface`. The produced target carries `18`'s
    ///        pre-added contribution and the resolved weight, which is what makes the composite expressible at
    ///        all — a target carrying the trace result instead would leave nothing to subtract.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Contribute(RenderSchedule& Schedule) const;

    /// 🧩 The extent the trace is resolved at, from one display extent.
    /// out   Result  [-]  refuses with ContentUnsupported for a display extent of nothing
    /// note  📐 Rounded **up** on both ordinates, matching `RenderSchedule`'s own fraction-of-display claim.
    ///        Rounding down leaves the display's last column with no coarse texel above it.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Resolve(std::uint32_t  DisplayX,
                          std::uint32_t  DisplayY,
                          std::uint32_t& ResolvedX,
                          std::uint32_t& ResolvedY) const;

    /// 🧩 What the caller must sample and where — one step of the march, in display coordinates.
    /// in    OriginX   [-]  the shading pixel, in the closed unit square
    /// in    OriginY  [-]  its second coordinate, likewise
    /// in    OriginDepth   [-]  its reversed depth
    /// in    StepX     [-]  the reflected direction projected into display coordinates, per step
    /// in    StepY    [-]  its second coordinate, likewise
    /// in    StepDepth     [-]  the reversed-depth change per step
    /// in    Sampling      [-]  answers with `DepthSurface` and `RadianceSurface` at a coordinate
    /// out   Traced        [-]  the crossing, or a weight of nothing
    /// note  🔴 The march is against `DepthSurface` at **half extent** — `30` §2 ③. Reflections are
    ///        low-frequency at all but mirror roughness, and the extent is where the cost lives.
    /// note  📝 The crossing is refined by a short binary search rather than by a finer march, because halving
    ///        the interval six times resolves it to within a sixty-fourth of one step and marching sixty-four
    ///        times as far costs sixty-four times as much for the same answer.
    /// note  🔴 The sampler is supplied rather than held, so that the same routine serves the device dispatch —
    ///        which samples a resident target — and `82`'s host preview, which samples a resolved one. `00` §11
    ///        gates the agreement between the two at Tier B, and one routine is the only way it holds.
    /// cost  🔴
    /// tag   api, nonthrowing
    template <typename Sampler>
    TracedReflection March(double        OriginX,
                           double        OriginY,
                           double        OriginDepth,
                           double        StepX,
                           double        StepY,
                           double        StepDepth,
                           const Sampler& Sampling) const;

    /// 🧩 Applies `30` §1's composite at one pixel.
    /// in    Current   [-]  `RadianceSurface` as `18` and `62` left it
    /// in    PreAdded   [-]  `ReflectionSurface` RGB — what `18`'s ambient term already contributed
    /// in    Traced     [-]  the trace's own result and weight
    /// out   Resolved   [-]  the amended radiance
    /// note  🔴 Through `Shared/`'s own routine and never written again here. The composite is the one piece of
    ///        arithmetic in this document that cannot be got slightly wrong without the error being invisible —
    ///        a double count brightens uniformly and reads as the material being wrong.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Compose(const double            Current[3],
                 const double            PreAdded[3],
                 const TracedReflection& Traced,
                 double                  Resolved[3]) const;

    /// 🧩 Records what one rotation's tracing cost, for `86`.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareRotation(std::uint32_t TracedCount, std::uint32_t ResolvedCount, std::uint32_t SkippedCount);

    /// 🧩 Declares every measure; appends nothing.
    /// note  🔴 `30` appears in **no** row of `86` §4's register, and this reports accordingly. A failed trace is
    ///        `30` §3's design operating and not a fact the artist needs told.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(MeasureIndex& Measured, TickPoint Sampled) const;

    const ReflectionSpecification& Declared() const;
    const ReflectionMetrics&       Metrics() const;

private:

    ReflectionSpecification  Specification = {};   // [-] - as Declare validated it
    ReflectionMetrics        Reported      = {};   // [-] - what `86` presents
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE MARCH
//------------------------------------------------------------------------------------------------------------------------

template <typename Sampler>
TracedReflection SpecularProjection::March(double         OriginX,
                                           double         OriginY,
                                           double         OriginDepth,
                                           double         StepX,
                                           double         StepY,
                                           double         StepDepth,
                                           const Sampler& Sampling) const
{
    TracedReflection Traced;

    double WalkingX  = OriginX;
    double WalkingY = OriginY;
    double WalkingDepth  = OriginDepth;

    for (std::uint32_t Step = 0u; Step < Specification.MarchLimit; ++Step)
    {
        const double PriorX  = WalkingX;
        const double PriorY = WalkingY;
        const double PriorDepth  = WalkingDepth;

        WalkingX  += StepX;
        WalkingY += StepY;
        WalkingDepth  += StepDepth;

        ++Traced.StepsTaken;

        // 🔴 Off the extent is a failure and a **termination**, not a clamp. Clamping walks the ray along the
        //    display edge and eventually reports a crossing against whatever surface happens to be there, which
        //    reads as a smear of the scene's border across every reflective surface facing outward.
        if (ReflectionLeftExtent(WalkingX, WalkingY))
            return Traced;

        const double RecordedDepth = Sampling.Depth(WalkingX, WalkingY);

        if (!ReflectionCrossed(WalkingDepth, RecordedDepth, Specification.ThicknessBound))
            continue;

        // 📐 The crossing lies between the previous step and this one. Halving that interval refines it without
        //    another depth march, and the six declared halvings resolve it to a sixty-fourth of a step.
        double LowerX  = PriorX;
        double LowerY = PriorY;
        double LowerDepth  = PriorDepth;
        double UpperX  = WalkingX;
        double UpperY = WalkingY;
        double UpperDepth  = WalkingDepth;

        for (std::uint32_t Refining = 0u; Refining < Specification.RefineCount; ++Refining)
        {
            const double MiddleX  = (LowerX  + UpperX)  * 0.5;
            const double MiddleY = (LowerY + UpperY) * 0.5;
            const double MiddleDepth  = (LowerDepth  + UpperDepth)  * 0.5;

            const double MiddleRecorded = Sampling.Depth(MiddleX, MiddleY);

            if (ReflectionCrossed(MiddleDepth, MiddleRecorded, Specification.ThicknessBound))
            {
                UpperX  = MiddleX;
                UpperY = MiddleY;
                UpperDepth  = MiddleDepth;
            }
            else
            {
                LowerX  = MiddleX;
                LowerY = MiddleY;
                LowerDepth  = MiddleDepth;
            }
        }

        Sampling.Radiance(UpperX, UpperY, Traced.Component);

        // 📐 The weight falls off toward the extent's edge rather than ending at it, so a reflection does not
        //    terminate in a hard line as the artist orbits. The falloff is over the outer tenth on each axis,
        //    which is wide enough to be invisible and narrow enough not to dim a reflection that is fully inside.
        const double MarginX  = 1.0 - std::fabs(UpperX  * 2.0 - 1.0);
        const double MarginY = 1.0 - std::fabs(UpperY * 2.0 - 1.0);
        const double Margin       = MarginX < MarginY ? MarginX : MarginY;

        Traced.Weight   = Margin >= 0.1 ? 1.0 : Margin / 0.1;
        Traced.Resolved = true;

        return Traced;
    }

    // 📝 The ceiling was reached with no crossing. Weight stays at nothing and `30` §1's guarantee makes that a
    //    no-op, which is why the ceiling can be tuned freely without a fallback path changing behaviour.
    return Traced;
}

// 📐 The composite and the march are Bounded; the extent test and the step count are Exact. `30` §1's target is
//    `RadianceSurface`, which is Tier D, so the component claims Perceptual — `00` §3's transitivity rule.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
