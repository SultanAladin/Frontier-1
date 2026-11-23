//============================================================================================================================================
//                                                           CHARTPARTITION.H
//============================================================================================================================================
// 🧩 The parametric domain every texturable surface addresses — cut, flattened, arranged, and measured.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateCompute/Compute/DomainSpace/Api/DomainSpace.h"
#include "SlateCompute/Compute/SeamSpecification/Api/SeamSpecification.h"
#include "SlateDocument/Document/TopologyConditioning/Api/TopologyConditioning.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateMath/Numeric/UnwrapSolver/Api/UnwrapSolver.h"
#include "SlateMath/Numeric/WorkSequence/Api/WorkSequence.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                               WHAT A PARTITION DECLARES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The parameters a partition is derived against.
/// note  🚧 `68` §10 leaves the distortion threshold open and it blocks quality alone — `86` reports the measured
///        distortion either way. It is declared here rather than in `Foundation/` because no second unit reads it.
/// tag   nonallocating, nonthrowing
struct PartitionSpecification
{
    double         DistortionThreshold  = 4.0;      // [-] - area ratio above which a chart is subdivided
    double         ConvergenceCriterion = 1.0e-7;   // [-] - handed to `UnwrapSolver`
    std::uint32_t  IterationLimit     = 4096u;    // [-] - handed to `UnwrapSolver`
    std::uint32_t  SubdivisionLimit   = 64u;      // [-] - subdivisions permitted before a chart is accepted
    bool           CommonScaleDeclared  = true;     // [-] - `68` §5's default
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE CHART
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One connected piece of topology bounded by seams and by the topology's own boundary.
/// note  🔴 Chart identity is the **least imported face ordinal** the chart holds. `24` §3 keys transferred
///        results on the partition revision, and a chart whose member set is unchanged keeps its least face
///        ordinal — so a partition that touched one seam does not renumber every chart and discard every derived
///        artefact for a change the artist cannot see.
/// tag   owning
struct Chart
{
    std::uint32_t              IdentityIndex = 0u;                                // [-]   - least face ordinal held
    std::vector<std::uint32_t> Faces           = {};                                // [-]   - imported face ordinals
    TerminationCause           Cause           = TerminationCause::CriterionSatisfied;
    double                     ResidualNorm    = 0.0;                               // [-]   - at termination
    std::uint32_t              IterationCount  = 0u;                                // [-]   - iterations taken
    DistortionMeasure          Distortion      = {};                                // [-]   - area and angle, apart
    std::uint32_t              SubdivisionCount = 0u;                               // [-]   - cuts this chart cost
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE METRICS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the partition reports through `86`.
/// tag   nonallocating, nonthrowing
struct PartitionMetrics
{
    std::uint32_t  ChartCount              = 0u;    // [-]   - charts the topology cut into
    std::uint32_t  DerivedSeamCount        = 0u;    // [-]   - cuts the partitioner added — `68` §2
    std::uint32_t  LimitTerminationCount = 0u;    // [-]   - charts that ended at the ceiling — `68` §4
    std::uint32_t  FoldCount               = 0u;    // [-]   - folds found and subdivided away — `68` §4.1
    double         Occupancy               = 0.0;   // [-]   - fraction of the domain covered — `68` §5
    double         MaximumAreaRatio       = 1.0;   // [-]   - worst across every chart
    double         MaximumAngleDeviation  = 0.0;   // [deg] - worst across every chart
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE DERIVED RESULT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A whole derived partition, as a value that crosses back to the tick.
/// note  🔴 A value rather than a mutation, because `68` §7 runs the flattening on `34`'s `Background` class and
///        `34` §3 requires the requester to apply the result on the tick. The previous partition stands until
///        `Adopt` takes this one — a surface with no valid domain has no place to sample and nothing for
///        `18` §1.1 to derive a tangent basis from, and the visible result of swapping mid-solve is the whole
///        object flickering while an unrelated seam is being marked.
/// tag   owning
struct DerivedPartition
{
    std::vector<Chart>             Charts            = {};   // [-] - in derivation order
    std::vector<DomainCoordinate>  CornerCoordinates = {};    // [-] - one per imported corner
    std::vector<SeamEdge>          DerivedSeams      = {};    // [-] - cuts the partitioner added
    PartitionMetrics               Metrics           = {};    // [-] - what `86` presents
    std::uint64_t                  DescribedRevision = 0u;    // [-] - the topology revision it describes
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DERIVATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Cuts, flattens and arranges one sealed topology into its parametric domain.
/// in    Imported     [-]  the sealed topology; immutable for the whole run
/// in    Conditioned  [-]  its conditioning, at the same revision
/// in    Seams        [-]  the authored seams; the derived set is produced here and returned, not written back
/// in    Declaring    [-]  the parameters
/// in    Cancellation [-]  read between charts — `34` §5's cooperative points
/// in    Progressed   [-]  charts resolved out of charts spanned
/// out   Result      [-]  refuses with HostDenied for an unsealed topology or a withdrawn declaration, and with
///                         ExtentExhausted when the conditioning describes another revision or no scale packs
/// note  🔴 Reads nothing but its arguments and mutates none of them, which is exactly `34` §2's requirement.
///        The `SeamSpecification` is taken by const reference and its derived set is returned in the result
///        rather than written into it, so a worker running this touches no document state at all.
/// note  🔴 A chart that is not a disc and a chart that folds are answered by the **same** mechanism: subdivide
///        and re-flatten, recording the crossing edges as derived seams. `68` §4.1 prescribes it for a fold, and
///        a chart with two boundary loops is the same failure one step earlier. Subdivision strictly reduces the
///        face count and a single face is always a disc that flattens without folding, so it terminates.
/// note  ⚠️ The boundary self-intersection test is quadratic in boundary length. That is affordable only because
///        `68` §7 puts this on `34`'s `Background` class; called on the tick it would stall a stroke.
/// cost  🔴
/// tag   api, nonthrowing
Deliver<DerivedPartition> Derive(const TopologyStructure&      Imported,
                                 const TopologyConditioning&   Conditioned,
                                 const SeamSpecification&      Seams,
                                 const PartitionSpecification& Declaring,
                                 const WorkCancellation&       Cancellation,
                                 WorkProgress&                 Progressed);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Convergent, PrecisionGuarantee::Convergent);

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE STANDING PARTITION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The partition a surface currently addresses, and the revision every derived artefact keys on.
/// note  🔴 `68` §6: a re-partition advances the partition revision, and `24` §3 keys transferred results on it
///        while `20` promotes against it. Re-partitioning moves domain positions, so every derived artefact
///        addressed in the old domain is invalid — and the revision is what makes that discoverable rather than
///        silent.
/// note  🔴 A camera move, an owner move and a texture stroke re-derive nothing here. The domain is parametric
///        rather than world-referred, which is `68` §6's table read from the storage side.
/// tag   owning
class ChartPartition
{
public:

    /// 🧩 Adopts a derived partition on the tick, advancing the revision.
    /// in    Incoming  [-]  as `Derive` produced it
    /// out   Result   [-]  refuses with ContentUnsupported for a partition carrying no chart
    /// post  the revision advanced; every artefact keyed on the prior one is discoverably stale
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Adopt(const DerivedPartition& Incoming);

    /// 🧩 The standing partition.
    /// pre   PartitionCurrent holds
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const DerivedPartition& Current() const;

    /// 🧩 One imported corner's domain coordinate.
    /// out   Result  [-]  refuses with ExtentExhausted outside the corner span, and with ContentUnsupported
    ///                     while no partition stands
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<DomainCoordinate> Coordinate(std::uint32_t CornerIndex) const;

    /// 🧩 Whether a partition stands at all.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool PartitionCurrent() const;

    /// 🧩 The revision `24` §3 keys on and `20` promotes against.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t Revision() const;

    /// 🧩 Appends the standing partition's obligations to the register, and its measures beside them.
    /// in    Reporting  [-]  where `68` §2's and §4's rows land
    /// in    Measured   [-]  where §5's occupancy and §4's distortion land
    /// in    Sampled    [ns] the tick's own reading
    /// note  🔴 Terminations and derived seams **append**; occupancy and distortion **overwrite**. `86` §2 draws
    ///        the line and it is the whole reason the register has two structures: a distortion appended every
    ///        partition would bury the one seam the artist did not expect.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(ReportSequence& Reporting, MeasureIndex& Measured, TickPoint Sampled) const;

private:

    DerivedPartition  CurrentPartition = {};    // [-] - as the last Adopt left it
    std::uint64_t     PartitionRevision = 0u;    // [-] - zero until the first Adopt
};

}   // namespace Slate
