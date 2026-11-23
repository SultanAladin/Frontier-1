//============================================================================================================================================
//                                                           SAMPLEINTEGRATOR.H
//============================================================================================================================================
// 🧩 `64` — one sample per rotation accumulated into convergence, reprojected by motion and reset rather than decayed.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "Shared/AccumulationProjection.slang.h"
#include "Shared/SampleProjection.slang.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REJECTIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Why one pixel's history was rejected.
/// note  🔴 On refusal the history is discarded and the count resets to one. It **resets rather than decays** —
///        `64` §4 — because a partial history of a surface that is no longer there is a coloured ghost, and a
///        ghost fading over ten rotations is more visible than one that is never drawn.
/// tag   guarantee
enum class RejectionSubject : std::uint32_t
{
    Accepted        = 0u,   // [-] - the history describes the same surface
    OffExtent       = 1u,   // [-] - the reprojected position is off the extent
    OwnerDiffers = 2u,   // [-] - a different surface resolved there — `16` §4.1
    DepthDiffers    = 3u,   // [-] - the same owner, a different part of it
    RejectionCount  = 4u    // [-] - the closed count, never a rejection
};

/// 🧩 What the accumulation refuses a history for.
/// note  🚧 Both bounds are `64` §9's open rows and each blocks tuning alone. They are declared here because no
///        second unit reads either — `00` §2's rule.
/// tag   nonallocating, nonthrowing
struct RejectionSpecification
{
    double         DepthBound         = 0.02;   // [-] - relative departure a reprojected depth may carry
    double         NeighbourhoodBound = 1.0;    // [-] - how far the neighbourhood is widened before bounding
    std::uint32_t  CountLimit       = 64u;    // [-] - samples before the weight stops falling
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 WHAT ONE PIXEL HOLDS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One pixel's accumulated result and the count the weight is derived from.
/// note  🔴 The count is **stored alongside** the value and is not derived from a rotation ordinal. Two pixels of
///        one rotation carry different counts wherever one of them was rejected, and a count derived from the
///        rotation would weight the reset pixel as though it had converged.
/// tag   nonallocating, nonthrowing
struct AccumulatedSample
{
    double         Component[3] = { 0.0, 0.0, 0.0 };   // [-] - working-space radiance
    std::uint32_t  SampleCount  = 0u;                  // [-] - samples accumulated; saturating
};

/// 🧩 What `64` reports through `86`.
/// tag   nonallocating, nonthrowing
struct ConvergenceMetrics
{
    std::uint32_t  MinimumSampleCount    = 0u;   // [-] - the least converged pixel of the rotation
    std::uint32_t  MaximumSampleCount = 0u;   // [-] - the most converged
    std::uint32_t  RejectedCount       = 0u;   // [-] - histories discarded this rotation
    std::uint32_t  AccumulatedCount    = 0u;   // [-] - pixels accumulated this rotation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `64` — the accumulation, the offset sequence it jitters by, and the invalidations that discard it whole.
/// note  🔴 `64` §2: reprojection reads `MotionSurface` and is **never** derived from depth and the previous
///        camera. Depth reprojection is correct only for what did not move, and what did move is exactly the
///        population this document has to handle. Motion covers the camera and the owner together, so the
///        accumulation does not need to know which happened.
/// note  🔴 `64` §5: accumulation happens **above** `08` §3 ⑧, on radiance. Accumulating display code averages
///        values that have been through a non-invertible tone projection, and the average of tone-mapped samples
///        is not the tone-mapped average — bright samples are compressed before averaging and the result is
///        darker than the scene is.
/// note  🔴 `64` §6: this reads its own previous result, which is the one place in the schedule where a rotation
///        slot depends on the one before it. `06`'s rotation is what makes that legal, and `Invalidate` is what
///        keeps it honest across the three moments where no previous result exists.
/// tag   owning
class SampleIntegrator
{
public:

    static constexpr std::uint32_t AmendmentIndex = 40u;   // [-] - `08` §3 ⑦, after `30`

    /// 🧩 Declares what a history is rejected for.
    /// out   Result  [-]  refuses with ContentUnsupported for a non-positive depth bound or a ceiling of nothing
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Declare(const RejectionSpecification& Declaring);

    /// 🧩 Contributes `08` §3 ⑦'s recording.
    /// out   Result  [-]  refuses with whatever the schedule rejected
    /// note  📝 Produces `AccumulationSurface` and amends nothing. It reads its own previous cycle slot, which
    ///        the schedule cannot express as a dependency and does not need to: `06`'s rotation orders the two.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Contribute(RenderSchedule& Schedule) const;

    /// 🧩 The sub-pixel offset one rotation carries, from `02` §6's sequence.
    /// in    RecordingIndex  [-]  the rotation, counted from bring-up
    /// out   OffsetX/Y        [px] within the pixel, never at its corner
    /// note  🔴 `64` §3.1: the offset is applied to `46`'s **projection** and never to a resolved position. An
    ///        offset applied after resolution shifts an already-resolved image, which resamples rather than
    ///        samples, and the result is blur rather than convergence.
    /// note  🔴 The sequence is `02` §6's and lives in `Shared/`. `46` applies it on the host and `82` replays it
    ///        for a preview; a sequence that disagreed would make a preview converge to a different image than
    ///        the workspace does — `64` §7's Tier A row.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void OffsetOf(std::uint64_t RecordingIndex, double& OffsetX, double& OffsetY) const;

    /// 🧩 Classifies whether one reprojected history may be accumulated into.
    /// in    ReprojectedX  [-]  where the pixel was last rotation
    /// in    ReprojectedY [-]
    /// in    HeldOwner     [-]  the owner resolved there last rotation
    /// in    IncomingOwner [-]  the owner resolved there now
    /// in    HeldDepth        [-]  reversed, as recorded last rotation
    /// in    IncomingDepth    [-]  reversed, as recorded now
    /// out   Subject          [-]  Accepted, or which of the three refusals applied
    /// note  🔴 The identity test reads `16` §4.1's owner resolution rather than the partition identity, so a
    ///        re-partition does not discard every pixel's history for a change nobody can see.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    RejectionSubject Classify(double        ReprojectedX,
                              double        ReprojectedY,
                              std::uint32_t HeldOwner,
                              std::uint32_t IncomingOwner,
                              double        HeldDepth,
                              double        IncomingDepth) const;

    /// 🧩 Accumulates one incoming sample into one pixel's history.
    /// in    Held      [-]  the reprojected history, amended in place
    /// in    Incoming  [-]  this rotation's radiance at the pixel
    /// in    Rejected   [-]  what Classify answered
    /// in    Minimum     [-]  the least of the incoming rotation's local neighbourhood, per component
    /// in    Maximum  [-]  the greatest of it, per component
    /// note  🔴 A refusal **resets** the count to one and writes the incoming sample whole. It does not decay —
    ///        `64` §4 — because a decaying ghost is more visible than an absent one.
    /// note  📝 The neighbourhood bound is applied before the accumulation and never after it. Applied after, the
    ///        bound would clamp a value the accumulation had already committed to and the count would then
    ///        describe a convergence toward something the pixel no longer holds.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Accumulate(AccumulatedSample& Held,
                    const double       Incoming[3],
                    RejectionSubject   Rejected,
                    const double       Minimum[3],
                    const double       Maximum[3]) const;

    /// 🧩 Discards every history, because no previous result describes anything.
    /// note  🔴 `64` §6 and §8: no history is read on the first rotation after bring-up, after an extent change,
    ///        or after a device loss. In all three the sample count starts at one — a history reprojected across
    ///        an extent change is a history addressed in pixels that no longer exist.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Invalidate();

    /// 🧩 Whether the accumulation may read a history at all this rotation.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool HistoryReadable() const;

    /// 🧩 Records what one rotation's accumulation found, for `86`.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareRotation(std::uint32_t MinimumSampleCount,
                         std::uint32_t MaximumSampleCount,
                         std::uint32_t RejectedCount,
                         std::uint32_t AccumulatedCount);

    /// 🧩 Declares every measure; appends nothing.
    /// note  🔴 `64` appears in no row of `86` §4's register. A rejected history is the mechanism working, and a
    ///        camera in motion rejects most of the extent every rotation.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(MeasureIndex& Measured, TickPoint Sampled) const;

    const RejectionSpecification& Declared() const;
    const ConvergenceMetrics&     Metrics() const;

private:

    RejectionSpecification  Specification   = {};      // [-] - as Declare validated it
    ConvergenceMetrics      Reported        = {};      // [-] - what `86` presents
    bool                    HistoryCurrent = false;   // [-] - false until one rotation has accumulated
};

// 📐 Owner identity, the cycle slot, the offset index and the sample count are Exact; the reprojected
//    position and the accumulated radiance are Bounded and Perceptual respectively — `64` §7.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
