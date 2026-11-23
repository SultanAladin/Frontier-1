//============================================================================================================================================
//                                                            UVSURFACEDEPOT.H
//============================================================================================================================================
// 🧩 `24` — attributes moved from a dense topology onto a sparse one through the domain, converging, and keyed by content.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateCompute/Compute/ChartPartition/Api/ChartPartition.h"
#include "SlateCompute/Compute/SurfaceDepot/Api/SurfaceDepot.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                               THE CORRESPONDENCE RULE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How one hit is chosen when the search within the extent returns several.
/// note  🔴 `24` §2 names the rule as a declared field rather than as a convention, because the two rules
///        disagree exactly where a dense source folds back on itself — and that is the region an artist notices,
///        not the region where every rule agrees.
/// tag   guarantee
enum class CorrespondenceSubject : std::uint32_t
{
    MinimumDeparture        = 0u,   // [-] - the nearest source surface, by distance alone
    MinimumAngularDeparture = 1u,   // [-] - the source surface most nearly along the working orientation
    CorrespondenceCount   = 2u    // [-] - the closed count, never a rule
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TRANSFER
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `24` §2's six fields — what is searched, how far, by which rule, for which channels, at what extent.
/// note  🔴 The search extent is a **ceiling** and never a starting point. Nothing widens it and nothing samples
///        beyond it: `24` §2 requires a miss to be recorded as a miss, and a value found past the extent is the
///        fabricated value that section refuses by name.
/// note  🔴 Tier C — the sweep converges against the criterion below and stops at the ceiling. Both figures are
///        declared here rather than in `Foundation/` because no second unit reads either; `00` §2's rule.
/// note  ⚠️ `Bake` is banned and this is not a euphemism for it. The operation is transfer, and its parameters
///        are these — `24`'s opening paragraph.
/// tag   nonallocating, nonthrowing
struct TransferSpecification
{
    double                 SearchExtent         = 1.0;   // [mm] - the greatest distance searched; never exceeded
    CorrespondenceSubject  Correspondence       = CorrespondenceSubject::MinimumDeparture;
    std::uint32_t          ChannelMask          = 0u;    // [-]  - one bit per `42` channel transferred
    std::uint32_t          DomainExtent         = 1024u; // [px] - the extent the result is written at
    double                 ConvergenceCriterion = 0.001; // [-]  - the sweep's residual below which it has converged
    std::uint32_t          IterationLimit     = 8u;    // [-]  - sweeps taken before termination is declared
    std::uint64_t          SpecificationIndex = 0u;    // [-]  - which transfer this is, for the content key
};

//------------------------------------------------------------------------------------------------------------------------
//                                                ONE CORRESPONDENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which source face one working position corresponds to, and how far away it stood.
/// note  🔴 Delivered only where the extent test accepted it. There is no "nearest anyway" delivery: a miss is
///        rejected with ExtentExhausted and the caller records it as a miss, which is diagnosable — a fabricated
///        correspondence is not.
/// tag   nonallocating, nonthrowing
struct SourceCorrespondence
{
    std::uint32_t  FaceIndex = 0u;    // [-]  - within the source topology, in its own ordering
    double         Departure   = 0.0;   // [mm] - from the working position to the corresponding face
};

//------------------------------------------------------------------------------------------------------------------------
//                                             WHAT THE TRANSFER MEASURED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one transfer resolved, and what it missed — per channel, as `24` §4 requires.
/// note  🔴 The miss count is **per channel** and not one total. A transfer that missed a tenth of the domain in
///        one channel and nothing in the other nineteen is visually similar to one that missed nothing, and a
///        single total says nothing about which attribute is wrong.
/// tag   nonallocating, nonthrowing
struct TransferMetrics
{
    std::uint32_t  DomainCount    = 0u;   // [-] - domain positions the sweep spanned
    std::uint32_t  ResolvedCount  = 0u;   // [-] - positions that found a correspondence within the extent
    std::uint32_t  ChannelMisses[static_cast<std::size_t>(ChannelSubject::ChannelCount)] = {};
    std::uint32_t  SweepCount     = 0u;   // [-] - sweeps taken, convergent or terminated
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TRANSFER
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `24` — the transfer, its content key, and its admission into `20`'s depot. No unwrap lives here.
/// note  🔴 `24` §5: this declares no unwrap, no seam classification and no chart packing, and reads no mechanism
///        from `20`. `68` produces the domain; the only `20` dependency is `SurfaceDepot` residency. That
///        one-way edge is what closed `00` §10's conflict 13, where `24` and `20` each depended on the other
///        and neither could be built first.
/// note  🔴 Every entry point reads its arguments and mutates none of them. The transfer is `34` `Background`
///        work by nature and the result crosses back to the tick as a value, exactly as `68`'s derivation does —
///        a transfer that wrote into the document as it ran could not be cancelled between sweeps.
/// tag   owning
class UvSurfaceDepot
{
public:

    // 📝 No face corresponds. Never a valid ordinal, and never conflated with face nought — a miss written as
    //    the first face is the fabricated correspondence `24` §2 refuses, wearing a plausible number.
    static constexpr std::uint32_t AbsentCorrespondence = 0xFFFFFFFFu;   // [-] - no source face within the extent

    /// 🧩 Declares the transfer parameters as one admission.
    /// in    Transferring_  [-]  the extent, the rule, the channels, the domain extent and the two Tier C figures
    /// out   Result        [-]  refuses with ContentUnsupported for a search extent of nothing, an empty channel
    ///                           mask, a domain extent of nothing, a criterion outside the unit interval, an
    ///                           iteration ceiling of nothing, and a rule outside the closed count
    /// post  the specification stands and the key below carries its ordinal
    /// note  🔴 An empty channel mask is rejected rather than accepted as a transfer of nothing. Accepted, it
    ///        reports no miss and no resolution and reads as a transfer that succeeded.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Declare(const TransferSpecification& Transferring_);

    /// 🧩 The content key one transferred result is held under — `24` §3's five fields, closed.
    /// in    Source        [-]  the dense origin; its seal revision is the first field
    /// in    Working       [-]  the sparse destination carrying the domain
    /// in    Partitioning  [-]  `68`'s standing partition, whose revision moves every domain position
    /// out   Result       [-]  refuses with ContentUnsupported before Declare, for an unsealed topology, and
    ///                          while no partition stands
    /// note  🔴 The partition revision is the field most easily left out and leaving it out is the defect that
    ///        matters. A result keyed without it survives a re-unwrap and is then read at positions that mean
    ///        something else, which presents as attributes subtly wrong everywhere rather than as a failure.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<ContentKey> KeyOf(const TopologyStructure& Source,
                              const TopologyStructure& Working,
                              const ChartPartition&    Partitioning) const;

    /// 🧩 The source face one working position corresponds to, within the declared extent.
    /// in    WorkingPosition     [mm] the reconstructed position on the working topology
    /// in    WorkingOrientation  [-]  its orientation, along which the angular rule measures
    /// in    Source              [-]  the dense topology searched
    /// out   Result             [-]  refuses with ExtentExhausted where nothing stands within the extent, and
    ///                                with ContentUnsupported before Declare
    /// note  🔴 The extent test is `IntersectionClassifier`'s volume overlap at Tier A — `24` §2 and §5's second
    ///        gate. A transfer that misclassifies which source surface corresponds produces seam artefacts
    ///        indistinguishable from unwrap defects, and the two then get debugged together for a long time.
    /// note  📝 The classification accepts or rejects; the rule below then chooses among what it accepted. The two
    ///        are apart because the admission is Exact and the choice is Bounded, and folding them would claim
    ///        the weaker guarantee for both.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<SourceCorrespondence> Correspond(DocumentPosition         WorkingPosition,
                                             SurfaceDirection         WorkingOrientation,
                                             const TopologyStructure& Source) const;

    /// 🧩 Sweeps the working topology's domain positions until the correspondence stops spreading.
    /// in    Source              [-]  the dense origin
    /// in    Working             [-]  the sparse destination
    /// in    SourceChannelMasks  [-]  which channels each source face carries; one entry per source face
    /// out   ConvergentResult    [-]  the metrics, the residual at termination, and which criterion ended it
    /// note  🔴 Returned as a `ConvergentResult` and never as a bare value — `02` §5. A transfer that stopped at
    ///        its ceiling and one that converged produce results that look identical, and the difference is
    ///        exactly what `24` §4 obliges this to report.
    /// note  📐 The residual is the fraction of positions **newly** resolved by the sweep. Later sweeps retry a
    ///        missed position against the face a resolved position on its own face corresponded to — and that
    ///        candidate must still pass the same extent test, so propagation never reaches past the extent.
    /// note  🔴 Whatever is still unresolved at termination is recorded as a miss. Nothing is filled from the
    ///        nearest value found beyond the extent, and nothing is filled with zero.
    /// cost  🔴
    /// tag   api, nonthrowing
    ConvergentResult<TransferMetrics> Transfer(const TopologyStructure&          Source,
                                               const TopologyStructure&          Working,
                                               const std::vector<std::uint32_t>& SourceChannelMasks) const;

    /// 🧩 Accepts one transferred result into `20`'s depot, as derived content.
    /// in    Depot            [-]  where derived artefacts live and are evicted from
    /// in    Keyed            [-]  as KeyOf produced it
    /// in    ByteExtent       [B]  what the result occupies
    /// in    RecordingIndex  [-]  the rotation it was derived on
    /// out   Result          [-]  refuses with whatever the depot rejected
    /// note  🔴 Declared as an analytic resolution and therefore reconstructible and evictable — `24` §5's last
    ///        gate. Nothing textured is ever stored here: a transferred result that has been textured over is a
    ///        layer above it in `56`, and the two are addressed at their own levels rather than merged.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Accept(SurfaceDepot&     Depot,
                        const ContentKey& Keyed,
                        std::uint64_t     ByteExtent,
                        std::uint64_t     RecordingIndex) const;

    /// 🧩 Appends the transfer's obligations to the register, and its measures beside them.
    /// in    Produced   [-]  as Transfer returned it
    /// in    Reporting  [-]  where the termination and the per-channel misses land
    /// in    Measured   [-]  where the counts land
    /// in    Sampled    [ns] the tick's own reading
    /// note  🔴 A termination at the ceiling and a channel that missed **append**; the counts **overwrite**. A
    ///        count appended every transfer buries the one channel that missed under readings nobody asked for —
    ///        `86` §2, and `68`'s reporting draws the same line.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(const ConvergentResult<TransferMetrics>& Produced,
                ReportSequence&                          Reporting,
                MeasureIndex&                            Measured,
                TickPoint                                Sampled) const;

    const TransferSpecification& Specification() const;

private:

    TransferSpecification  Transferring      = {};      // [-] - as Declare validated it
    bool                   TransferCurrent  = false;   // [-] - false until one declaration was accepted
};

// 📐 The extent classification is Exact and the departure arithmetic is Bounded; the sweep terminates against a
//    declared criterion and is therefore Convergent. `00` §3's transitivity rule folds to the weakest, and the
//    component claims Convergent — which is `24`'s own tier.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Convergent,
                         PrecisionGuarantee::Convergent,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
