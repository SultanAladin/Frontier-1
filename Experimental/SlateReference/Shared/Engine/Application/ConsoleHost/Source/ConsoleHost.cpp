//============================================================================================================================================
//                                                             CONSOLEHOST.CPP
//============================================================================================================================================
// 🧩 Headless bring-up in link order — every unit constructed, reported, and reclaimed without a window.

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"

#include "Shared/ContainmentClassifier.slang.h"
#include "Shared/IncircleClassifier.slang.h"
#include "Shared/IntersectionClassifier.slang.h"
#include "Shared/OrientationClassifier.slang.h"
#include "Shared/SampleProjection.slang.h"

#include "SlateMath/Platform/TickSequence/Api/TickSequence.h"
#include "SlateMath/Platform/InputExchange/Api/InputExchange.h"
#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateMath/Numeric/WorkSequence/Api/WorkSequence.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"

#include "SlateDocument/Document/PopulationIndex/Api/PopulationIndex.h"
#include "SlateDocument/Document/RevisionSequence/Api/RevisionSequence.h"
#include "SlateDocument/Document/PropertySpecification/Api/PropertySpecification.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"
#include "SlateDocument/Document/TopologyConditioning/Api/TopologyConditioning.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/VectorInterchange/Api/VectorInterchange.h"
#include "SlateDocument/Document/CameraProjection/Api/CameraProjection.h"
#include "SlateDocument/Document/IlluminantPopulation/Api/IlluminantPopulation.h"
#include "SlateDocument/Document/SpatialSubdivision/Api/SpatialSubdivision.h"
#include "SlateDocument/Document/IntakeIndex/Api/IntakeIndex.h"
#include "SlateDocument/Document/AssetInterchange/Api/AssetInterchange.h"
#include "SlateDocument/Format/FormatCodec/Api/FormatCodec.h"

#include "SlateMath/Numeric/CurveSolver/Api/CurveSolver.h"
#include "SlateMath/Numeric/UnwrapSolver/Api/UnwrapSolver.h"

#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"

#include "SlateCompute/Compute/AtmosphereIntegrator/Api/AtmosphereIntegrator.h"
#include "SlateCompute/Compute/ImpressionSequence/Api/ImpressionSequence.h"
#include "SlateCompute/Compute/ParityRunner/Api/ParityRunner.h"
#include "SlateCompute/Compute/TransmissionSequence/Api/TransmissionSequence.h"
#include "SlateCompute/Compute/SpecularProjection/Api/SpecularProjection.h"
#include "SlateCompute/Compute/SampleIntegrator/Api/SampleIntegrator.h"
#include "SlateCompute/Compute/DisplayProjection/Api/DisplayProjection.h"
#include "SlateCompute/Compute/SeamSpecification/Api/SeamSpecification.h"
#include "SlateCompute/Compute/DomainSpace/Api/DomainSpace.h"
#include "SlateCompute/Compute/ChartPartition/Api/ChartPartition.h"
#include "SlateDocument/Document/BrushSpecification/Api/BrushSpecification.h"
#include "SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h"
#include "SlateDocument/Document/PointerIntersection/Api/PointerIntersection.h"
#include "SlateDocument/Document/ToolSequence/Api/ToolSequence.h"

// 📝 🔴 This host names no interface component and constructs no instance. `00` §2.2 keeps exactly one copy
//    of ImGui, compiled inside SlateUI; a headless host reaching for it would be linking a window-system
//    attachment it can never construct. `SlateUI` and the vendor edge belong to the windowed host.

#include <cstdio>
#include <cstring>
#include <cmath>
#include <atomic>
#include <thread>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      REPORTING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every check reports through one routine, so a run's output reads as one table rather than as a
//    sequence of independently phrased sentences.
int RejectedCount = 0;   // [-] - checks that did not hold; also the process exit ordinal

void Report(const char* Subject, bool Held, const char* Detail)
{
    if (!Held)
        ++RejectedCount;

    std::printf("  %-42s %-8s %s\n", Subject, Held ? "held" : "REFUSED", Detail);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 PLATFORM AND NUMERIC
//------------------------------------------------------------------------------------------------------------------------

// 📝 SlateMath is verified first because every unit below consumes something declared there, and a failure
//    here would otherwise be reported by whichever unit happened to read the result.
void VerifyMathematics()
{
    std::printf("SlateMath\n");

    const Slate::TickSequence HostTimeline;

    const Slate::TickPoint EarlierReading = HostTimeline.Advance();
    const Slate::TickPoint LaterReading   = HostTimeline.Advance();

    Report("TickSequence monotonic",
           LaterReading.Index >= EarlierReading.Index,
           "[ns] never decreasing between two reads");

    Report("TickSequence span never negative",
           Slate::TickSequence::Span(LaterReading, EarlierReading) == 0.0,
           "[ms] reversed operands report zero");

    // 📝 The arrival extent is bounded and non-allocating, so overrunning it must discard the oldest sample
    //    rather than grow. Recording one more than the extent holds is the only way to observe that.
    Slate::InputExchange Arrivals;

    for (std::uint32_t Index = 0u; Index <= Slate::InputExchange::ArrivalCapacity; ++Index)
    {
        Slate::PointerSample Incoming;
        Incoming.Arrival.Index = Index;
        Incoming.PositionX       = static_cast<double>(Index);
        Arrivals.Record(Incoming);
    }

    Report("InputExchange extent bounded",
           Arrivals.HeldCount() == Slate::InputExchange::ArrivalCapacity,
           "[-] the extent never grows");

    Report("InputExchange discards the oldest",
           Arrivals.Sample(0u).Arrival.Index == 1ull,
           "[-] a discard, not a corrupted ordering");

    Arrivals.Reclaim();

    Report("InputExchange reclaimed", Arrivals.HeldCount() == 0u, "[-] drained by the consumer");

    // 📐 Compounding a rotation with its conjugate returns the identity. The residue is bounded rather than
    //    zero, which is exactly what the declared Bounded guarantee claims.
    Slate::RotationQuaternion QuarterTurn;
    QuarterTurn.ImaginaryZ = 0.7071067811865476;
    QuarterTurn.Real       = 0.7071067811865476;

    Slate::RotationQuaternion Conjugate = QuarterTurn;
    Conjugate.ImaginaryX = -QuarterTurn.ImaginaryX;
    Conjugate.ImaginaryY = -QuarterTurn.ImaginaryY;
    Conjugate.ImaginaryZ = -QuarterTurn.ImaginaryZ;

    const Slate::RotationQuaternion Restored = Slate::Compound(QuarterTurn, Conjugate);
    const double                    RealGap  = Restored.Real - 1.0;

    Report("Rotation compounds to the identity",
           RealGap < Slate::WeldTolerance && RealGap > -Slate::WeldTolerance,
           "[-] within the declared tolerance");

    // 📝 🔴 The rebasing subtraction happens in 64-bit. A document position a billion millimetres out,
    //    displaced by one millimetre, survives the narrowing only because the subtraction preceded it.
    //    `02` §8 gates this, and the failure it prevents reads as a driver defect rather than as arithmetic.
    Slate::DocumentPosition DistantPosition;
    DistantPosition.PositionX = 1.0e9;

    Slate::DocumentPosition ViewOrigin;
    ViewOrigin.PositionX = 1.0e9 - 1.0;

    const Slate::DevicePosition Rebased = Slate::Rebase(DistantPosition, ViewOrigin);

    Report("Rebasing precedes narrowing",
           Rebased.PositionX == 1.0f,
           "[mm] one millimetre survives at 1e9");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE EXACT PREDICATES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The predicates are verified here rather than only inside `ParityRunner`, because the runner proves
//    self-consistency and this proves the answers. A predicate that is antisymmetric and wrong passes the
//    runner; a predicate that says a position outside a circle is inside it does not pass this.
void VerifyClassifiers()
{
    std::printf("Shared predicates\n");

    // 📐 The unit right triangle, counter-clockwise, with circumcentre at (0.5, 0.5).
    Report("Incircle resolves inside",
           Slate::ClassifyIncircle(0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.4, 0.4) > 0,
           "[-] for a counter-clockwise triangle");

    Report("Incircle resolves outside",
           Slate::ClassifyIncircle(0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 2.0, 2.0) < 0,
           "[-] and the sign is unambiguous");

    // 🔴 The exact path decides this one: the position is on the circle and no filter can say so.
    Report("Incircle resolves cocircular exactly",
           Slate::ClassifyIncircle(0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0) == 0,
           "[-] the arena, not the filter");

    Report("Incircle inverts with the winding",
           Slate::ClassifyIncircle(0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.4, 0.4) < 0,
           "[-] which is why the unwound form exists");

    Report("The unwound form does not",
           Slate::ClassifyIncircleUnwound(0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.4, 0.4) > 0
        && Slate::ClassifyIncircleUnwound(0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.4, 0.4) > 0,
           "[-] the same answer either way round");

    Report("A degenerate triangle describes no circle",
           Slate::ClassifyIncircleUnwound(0.0, 0.0, 1.0, 1.0, 2.0, 2.0, 3.0, 3.0) == 0,
           "[-] nothing is inside it");

    Report("Segments cross transversally",
           Slate::ClassifySegmentIntersection(0.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0, 0.0)
        == SlateIntersectionCrossing,
           "[-] interiors, strictly");

    Report("An endpoint on a segment touches",
           Slate::ClassifySegmentIntersection(0.0, 0.0, 1.0, 0.0, 0.5, 0.0, 0.5, 1.0)
        == SlateIntersectionTouching,
           "[-] never reported as a crossing");

    // 📐 Vertical and collinear — the case that makes the overlap axis a decision rather than a convenience.
    Report("Collinear vertical segments overlap",
           Slate::ClassifySegmentIntersection(0.0, 1.0, 0.0, 3.0, 0.0, 2.0, 0.0, 4.0)
        == SlateIntersectionCollinear,
           "[-] the axis is chosen by span");

    Report("Collinear segments meeting at one position touch",
           Slate::ClassifySegmentIntersection(0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 2.0, 0.0)
        == SlateIntersectionTouching,
           "[-] one position is not a span");

    Report("Parallel segments are disjoint",
           Slate::ClassifySegmentIntersection(0.0, 0.0, 1.0, 1.0, 2.0, 0.0, 3.0, 1.0)
        == SlateIntersectionDisjoint,
           "[-] and no crossing is resolved");

    double CrossingX = 0.0;
    double CrossingY = 0.0;

    Report("A crossing position resolves",
           Slate::ResolveSegmentCrossing(0.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0, 0.0, CrossingX, CrossingY)
        && std::fabs(CrossingX - 0.5) < 1.0e-12
        && std::fabs(CrossingY - 0.5) < 1.0e-12,
           "[-] Bounded, and named apart from the classification");

    Report("A parallel pair resolves no position",
           !Slate::ResolveSegmentCrossing(0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0, CrossingX, CrossingY),
           "[-] refuses rather than dividing by zero");

    Report("Extents overlapping interiors",
           Slate::ClassifyExtentOverlap(0.0, 0.0, 2.0, 2.0, 1.0, 1.0, 3.0, 3.0) > 0,
           "[-] +1");

    Report("Extents touching on a bound",
           Slate::ClassifyExtentOverlap(0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 2.0, 1.0) == 0,
           "[-] 0 — which `38` §6's outward rounding turns into a hit, never a miss");

    Report("Extents disjoint",
           Slate::ClassifyExtentOverlap(0.0, 0.0, 1.0, 1.0, 2.0, 2.0, 3.0, 3.0) < 0,
           "[-] −1");

    Report("Volumes overlapping interiors",
           Slate::ClassifyVolumeOverlap(0.0, 0.0, 0.0, 2.0, 2.0, 2.0,
                                        1.0, 1.0, 1.0, 3.0, 3.0, 3.0) > 0,
           "[-] +1");

    Report("Volumes touching on a face",
           Slate::ClassifyVolumeOverlap(0.0, 0.0, 0.0, 1.0, 1.0, 1.0,
                                        1.0, 0.0, 0.0, 2.0, 1.0, 1.0) == 0,
           "[-] 0 — outward rounding turns it into a hit, never a miss");

    // 🔴 The case three planar tests get wrong: every axis-aligned projection overlaps and the volumes do not.
    //    Composing the planar predicate three times would report +1 here.
    Report("Volumes disjoint on one axis alone",
           Slate::ClassifyVolumeOverlap(0.0, 0.0, 0.0, 1.0, 1.0, 1.0,
                                        0.5, 0.5, 2.0, 1.5, 1.5, 3.0) < 0,
           "[-] −1, where the planar form would resolve +1");

    Report("An interval strictly contains another",
           Slate::ClassifyIntervalContainment(0u, 100u, 10u, 20u) > 0,
           "[-] `12` §2.1's comparison");

    Report("An interval does not contain itself",
           Slate::ClassifyIntervalContainment(0u, 100u, 0u, 100u) == 0,
           "[-] identity, never containment");

    Report("A shared bound is not containment",
           Slate::ClassifyIntervalContainment(0u, 100u, 0u, 50u) < 0,
           "[-] invariant 4 nests strictly");

    Report("Overlapping intervals contain neither",
           Slate::ClassifyIntervalContainment(0u, 50u, 40u, 60u) < 0
        && Slate::ClassifyIntervalContainment(40u, 60u, 0u, 50u) < 0,
           "[-] and invariant 4 forbids the shape outright");

    Report("Registration is inclusive at both bounds",
           Slate::IndexRegistered(10u, 20u, 10u)
        && Slate::IndexRegistered(10u, 20u, 20u)
        && !Slate::IndexRegistered(10u, 20u, 21u),
           "[-] where the label comparison is strict");

    Report("Disjointness is symmetric",
           Slate::IntervalsDisjoint(0u, 10u, 20u, 30u)
        && Slate::IntervalsDisjoint(20u, 30u, 0u, 10u)
        && !Slate::IntervalsDisjoint(0u, 25u, 20u, 30u),
           "[-] invariant 4, as one comparison");

    // 📐 The base-two inverse of an even ordinal and of its successor differ by exactly one half, in binary,
    //    with no tolerance. Nothing else in the engine can be asserted this strongly.
    bool RadicalHeld = true;

    for (std::uint32_t Index = 0u; Index < 1024u; Index += 2u)
    {
        if (Slate::ProjectRadicalTwo(Index + 1u) - Slate::ProjectRadicalTwo(Index) != 0.5)
            RadicalHeld = false;
    }

    Report("The base-two sequence steps exactly", RadicalHeld, "[-] one half, bit for bit");

    bool WithinUnitSquare = true;

    for (std::uint32_t Index = 0u; Index < 4096u; ++Index)
    {
        double FirstCoordinate  = 0.0;
        double SecondCoordinate = 0.0;
        Slate::ProjectPlanarSample(Index, FirstCoordinate, SecondCoordinate);

        if (FirstCoordinate < 0.0 || FirstCoordinate >= 1.0
         || SecondCoordinate < 0.0 || SecondCoordinate >= 1.0)
        {
            WithinUnitSquare = false;
        }
    }

    Report("Planar samples stay in the unit square", WithinUnitSquare, "[-] half-open, at every ordinal");

    Report("The base-three inverse is exact at its first digits",
           Slate::ProjectRadicalThree(1u) == 1.0 / 3.0
        && Slate::ProjectRadicalThree(3u) == 1.0 / 9.0,
           "[-] one correctly-rounded division, never twenty");

    // 📝 The offsets sit within the pixel and never at its corner, which is why the sequence begins at one.
    bool OffsetsWithinPixel = true;

    for (std::uint32_t Index = 0u; Index < 64u; ++Index)
    {
        double OffsetX = 0.0;
        double OffsetY = 0.0;
        Slate::ProjectSubPixelOffset(Index, OffsetX, OffsetY);

        if (OffsetX < -0.5 || OffsetX >= 0.5 || OffsetY < -0.5 || OffsetY >= 0.5)
            OffsetsWithinPixel = false;

        if (OffsetX == -0.5 && OffsetY == -0.5)
            OffsetsWithinPixel = false;
    }

    Report("Sub-pixel offsets stay within the pixel",
           OffsetsWithinPixel,
           "[px] and never at its corner");

    Report("The offset sequence repeats on its declared length",
           [&]
           {
               double FirstX = 0.0, FirstY = 0.0, LaterX = 0.0, LaterY = 0.0;
               Slate::ProjectSubPixelOffset(3u, FirstX, FirstY);
               Slate::ProjectSubPixelOffset(3u + Slate::SubPixelSequenceLength, LaterX, LaterY);
               return FirstX == LaterX && FirstY == LaterY;
           }(),
           "[-] `46`, `64` and `82` read one length");

    // 📐 Bounded, so measured against the declared bound rather than compared for equality.
    double MaximumDeviation = 0.0;

    for (std::uint32_t Index = 0u; Index < 1024u; ++Index)
    {
        double FirstCoordinate  = 0.0;
        double SecondCoordinate = 0.0;
        Slate::ProjectPlanarSample(Index, FirstCoordinate, SecondCoordinate);

        double DirectionX = 0.0;
        double DirectionY = 0.0;
        double DirectionZ = 0.0;
        Slate::ProjectSphericalSample(FirstCoordinate, SecondCoordinate,
                                      DirectionX, DirectionY, DirectionZ);

        const double Length    = std::sqrt(DirectionX * DirectionX
                                         + DirectionY * DirectionY
                                         + DirectionZ * DirectionZ);
        const double Deviation = std::fabs(Length - 1.0) / Slate::MachineEpsilon;

        if (Deviation > MaximumDeviation)
            MaximumDeviation = Deviation;
    }

    Report("Spherical samples are unit length",
           MaximumDeviation <= Slate::SampleUnitPlaceLimit,
           "[-] within the declared bound in units in the last place");

    bool HemisphereHeld = true;

    for (std::uint32_t Index = 0u; Index < 1024u; ++Index)
    {
        double FirstCoordinate  = 0.0;
        double SecondCoordinate = 0.0;
        Slate::ProjectPlanarSample(Index, FirstCoordinate, SecondCoordinate);

        double DirectionX = 0.0;
        double DirectionY = 0.0;
        double DirectionZ = 0.0;
        Slate::ProjectHemisphericalSample(FirstCoordinate, SecondCoordinate,
                                          DirectionX, DirectionY, DirectionZ);

        if (DirectionZ < 0.0)
            HemisphereHeld = false;
    }

    Report("Hemispherical samples stay on one side",
           HemisphereHeld,
           "[-] `60` §5 samples the closed hemisphere");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE REGISTER
//------------------------------------------------------------------------------------------------------------------------

// 📝 The distinction `86` §2 rests on is the only thing worth measuring here: a measure must overwrite and a
//    report must append. A register that appended `06` §3's rotational totals is the defect, and it is a defect
//    nothing else in the engine can observe.
void VerifyReporting()
{
    std::printf("ReportSequence\n");

    const Slate::TickSequence HostTimeline;
    Slate::ReportSequence     Reporting;

    Slate::ReportSpecification Rejected;
    Rejected.Origin      = "06 §3 ByteSpace";
    Rejected.Subject     = "Reserved";
    Rejected.Detail      = "the reserved claim could not be satisfied in full";
    Rejected.Verdict = Slate::ReportVerdict::Rejected;
    Rejected.Arrival     = HostTimeline.Advance();

    Reporting.Append(Rejected);

    Report("A report appends", Reporting.RetainedCount() == 1u, "[-] exactly one entry");

    Reporting.Append(Rejected);
    Reporting.Append(Rejected);

    Report("A recurrence coalesces",
           Reporting.RetainedCount() == 1u && Reporting.AppendedCount() == 3u,
           "[-] one entry, three occurrences");

    // 🔴 `86` §6: coalescing is by origin, verdict and subject together. Coalescing by origin alone would
    //    present twelve distinct rejected constructs as one entry with a count of twelve.
    Slate::ReportSpecification OtherSubject = Rejected;
    OtherSubject.Subject                    = "Committed";

    Reporting.Append(OtherSubject);

    Report("A different subject is its own entry",
           Reporting.RetainedCount() == 2u,
           "[-] never coalesced by origin alone");

    Slate::ReportSpecification OtherVerdict = Rejected;
    OtherVerdict.Verdict               = Slate::ReportVerdict::Truncated;

    Reporting.Append(OtherVerdict);

    Report("A different verdict is its own entry",
           Reporting.RetainedCount() == 3u,
           "[-] the verdict discriminates too");

    const std::vector<Slate::ReportSpecification> Retained = Reporting.Retained();

    Report("Retention is oldest first",
           Retained.size() == 3u && Retained[0].OccurrenceCount == 3u,
           "[-] the coalesced entry carries its count");

    // 📝 The bound is measured rather than assumed, because `86` §6 requires the discard itself to be presented.
    //    A register that silently forgot the first report of a run is worse than one that accepts it is full.
    for (std::uint32_t Index = 0u; Index <= Slate::ReportSequence::RetainedLimit; ++Index)
    {
        Slate::ReportSpecification Filling;
        Filling.Origin         = "34 §5 WorkSequence";
        Filling.Subject        = "Filling";
        Filling.SubjectIndex = Index;
        Filling.Verdict    = Slate::ReportVerdict::Failed;

        Reporting.Append(Filling);
    }

    Report("Retention is bounded",
           Reporting.RetainedCount() == Slate::ReportSequence::RetainedLimit,
           "[-] the ceiling holds");

    Report("The discard is itself counted",
           Reporting.DiscardedCount() != 0u,
           "[-] presented, never silent");

    Slate::MeasureIndex Measured;

    Measured.DeclareCount("06 §3 ByteSpace", "Reserved", 1024u, HostTimeline.Advance());
    Measured.DeclareCount("06 §3 ByteSpace", "Reserved", 2048u, HostTimeline.Advance());

    const Slate::Deliver<Slate::SampledMeasure> Resolved = Measured.Resolve("06 §3 ByteSpace", "Reserved");

    Report("A measure overwrites",
           Measured.Measures().size() == 1u && Resolved.Resolved && Resolved.Resolve().Counted == 2048ull,
           "[-] one entry, the latest reading");

    Report("An undeclared measure refuses",
           !Measured.Resolve("06 §3 ByteSpace", "Available").Resolved,
           "[-] refuses rather than reading zero");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WORK OFF THE TICK
//------------------------------------------------------------------------------------------------------------------------

void VerifyWork()
{
    std::printf("WorkSequence\n");

    const Slate::TickSequence HostTimeline;
    Slate::ReportSequence     Reporting;
    Slate::WorkSequence       Working;

    Report("A declaration before Construct is rejected",
           !Working.Declare(Slate::WorkDeclaration{}).Resolved,
           "[-] no worker stands to resolve it");

    Report("Workers construct",
           Working.ConstructWorkerSequence(4u, HostTimeline, Reporting).Resolved && Working.WorkerCount() == 4u,
           "[-] the count is fixed and recorded");

    Report("A second Construct is rejected",
           !Working.ConstructWorkerSequence(4u, HostTimeline, Reporting).Resolved,
           "[-] the workers are constructed once");

    // 📝 The resolution reads only what is captured here — `34` §2. Nothing it touches is the document, the
    //    tick's state, or anything in `76`, which is the rule that makes every lock in the sequence unnecessary.
    std::atomic<std::uint32_t> ResolvedCount { 0u };

    Slate::WorkDeclaration Declaring;
    Declaring.Origin   = "ConsoleHost";
    Declaring.Priority = Slate::WorkPriority::Interactive;
    Declaring.Resolve  = [&ResolvedCount](const Slate::WorkCancellation&, Slate::WorkProgress& Progressed)
    {
        Progressed.DeclareCount(8u, 8u);
        ResolvedCount.fetch_add(1u, std::memory_order_relaxed);

        return Slate::Deliver<bool>::Result(true);
    };

    std::vector<Slate::WorkIdentity> Declared;

    for (std::uint32_t Index = 0u; Index < 32u; ++Index)
    {
        const Slate::Deliver<Slate::WorkIdentity> Registered = Working.Declare(Declaring);

        if (Registered.Resolved)
            Declared.push_back(Registered.Resolve());
    }

    Report("Every declaration was accepted", Declared.size() == 32u, "[-] none silently dropped");

    // 📝 Drained repeatedly rather than waited on. `34` §3 makes the tick the only place a result is applied,
    //    and a host that blocked on a condition here would be observing the sequence from outside its guarantee.
    // 🔴 Each drain is checked for ordering as it arrives. `34` §6's guarantee is **within one drain** and is not
    //    a global prefix: a conclusion is delivered as soon as it is recorded, so accumulating several drains and
    //    asserting the accumulation is ordered asserts a property no bounded worker count can supply. Supplying
    //    it would mean holding a conclusion back until every earlier declaration had also completed, and that is
    //    the starvation `34` §4 forbids — a `Background` export declared first would block every `Interactive`
    //    promotion declared after it from ever being applied.
    std::vector<Slate::WorkCompletion> Completed;
    bool                               OrderHeld = true;

    for (std::uint32_t Passed = 0u; Passed < 100000u && Completed.size() < 32u; ++Passed)
    {
        const std::vector<Slate::WorkCompletion>& Drained = Working.Drain();

        for (std::size_t Index = 1u; Index < Drained.size(); ++Index)
        {
            if (Drained[Index - 1u].DeclaredIndex >= Drained[Index].DeclaredIndex)
                OrderHeld = false;
        }

        for (const Slate::WorkCompletion& Held : Drained)
            Completed.push_back(Held);

        // A tight bounded drain can consume its whole allowance before a worker receives a time slice,
        // particularly on a single-core validation runner. Yielding an empty pass preserves non-blocking
        // observation while allowing the declared work to make progress.
        if (Drained.empty())
            std::this_thread::yield();
    }

    Report("Every declaration completed", Completed.size() == 32u, "[-] each crossed back exactly once");

    Report("Every resolution ran",
           ResolvedCount.load(std::memory_order_relaxed) == 32u,
           "[-] once each, never twice");

    Report("Each drain is ordered by declaration",
           OrderHeld,
           "[-] within the drain, never by which worker finished first");

    // 🔴 The property `34` §6 genuinely binds: every declaration is completed exactly once, so the results of a
    //    split solve recombine by declared index. A conclusion delivered twice, or one lost between drains, is
    //    what would make the same inputs produce two documents on two machines.
    bool RecombinationHeld = Completed.size() == 32u;

    for (std::uint32_t Expected = 1u; Expected <= 32u; ++Expected)
    {
        std::uint32_t Found = 0u;

        for (const Slate::WorkCompletion& Held : Completed)
        {
            if (Held.DeclaredIndex == static_cast<std::uint64_t>(Expected))
                ++Found;
        }

        if (Found != 1u)
            RecombinationHeld = false;
    }

    Report("Every declared index completed once",
           RecombinationHeld,
           "[-] recombination by index, never by completion");

    bool DeliveredThroughout = true;

    for (const Slate::WorkCompletion& Held : Completed)
    {
        if (Held.Completed != Slate::WorkConclusion::Delivered)
            DeliveredThroughout = false;
    }

    Report("Each delivered", DeliveredThroughout, "[-] no spurious cancellation");

    Report("A completed identity no longer resolves",
           !Declared.empty() && !Working.Progress(Declared[0]).Resolved,
           "[-] the generation advanced at Seal");

    // 📝 A refusing resolution is reported through `86` with its origin — `34` §5. A refusal that produced no
    //    report would leave the artist unable to say why a solve produced nothing.
    Slate::WorkDeclaration Refusing;
    Refusing.Origin   = "ConsoleHost refusal";
    Refusing.Priority = Slate::WorkPriority::Background;
    Refusing.Resolve  = [](const Slate::WorkCancellation&, Slate::WorkProgress&)
    {
        return Slate::Deliver<bool>::Refuse({ Slate::RefusalReason::ExtentExhausted, "rejected deliberately" });
    };

    const Slate::Deliver<Slate::WorkIdentity> Declining = Working.Declare(Refusing);

    bool RefusalCompleted = false;

    for (std::uint32_t Passed = 0u; Passed < 100000u && !RefusalCompleted; ++Passed)
    {
        for (const Slate::WorkCompletion& Held : Working.Drain())
        {
            if (Held.Completed == Slate::WorkConclusion::Rejected)
                RefusalCompleted = true;
        }
    }

    Report("A refusal concludes as rejected",
           Declining.Resolved && RefusalCompleted,
           "[-] carrying its reason");

    Report("A refusal reaches the register",
           Reporting.RetainedCount() != 0u,
           "[-] `34` §5 reports through `86`");

    Working.Reclaim();

    Report("Reclamation joins every worker", Working.WorkerCount() == 0u, "[-] nothing is left running");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       COLOUR
//------------------------------------------------------------------------------------------------------------------------

void VerifyColour()
{
    std::printf("ColourProjection\n");

    const Slate::ColourSpaceSpecification Working = Slate::DeclaredWorkingSpace();
    const Slate::ColourSpaceSpecification Display = Slate::DeclaredDisplaySpace();

    Report("The two spaces are distinct",
           Working.SpaceIdentity != Display.SpaceIdentity,
           "[-] the display space is never the working space");

    Slate::ColourSpecification Undeclared;
    Undeclared.RedCoordinate = 0.5;

    Report("An undeclared colour refuses projection",
           !Slate::Project(Undeclared, Working, Display).Resolved,
           "[-] no bare triple is projected");

    // 📐 A neutral working coordinate must project to a neutral display coordinate. `66` §3 requires the tone
    //    projection to preserve the neutral axis, and it cannot if the space projection does not.
    Slate::ColourSpecification Neutral;
    Neutral.RedCoordinate   = 0.5;
    Neutral.GreenCoordinate = 0.5;
    Neutral.BlueCoordinate  = 0.5;
    Neutral.SpaceIdentity   = Slate::WorkingSpaceIdentity;

    const Slate::Deliver<Slate::ColourSpecification> Projected = Slate::Project(Neutral, Working, Display);

    const bool NeutralHeld = Projected.Resolved
                          && std::fabs(Projected.Resolve().RedCoordinate
                                     - Projected.Resolve().GreenCoordinate) < 1.0e-6
                          && std::fabs(Projected.Resolve().GreenCoordinate
                                     - Projected.Resolve().BlueCoordinate) < 1.0e-6;

    Report("Neutral projects to neutral", NeutralHeld, "[-] the axis survives the white adaptation");

    // 📐 Projected back, the coordinate returns to itself within the Bounded guarantee. A transfer applied twice
    //    or omitted once is the most common defect in a display path and it reads as "a bit washed out".
    const Slate::Deliver<Slate::ColourSpecification> Returned =
        Projected.Resolved ? Slate::Project(Projected.Resolve(), Display, Working)
                                 : Projected;

    Report("A round trip returns the coordinate",
           Returned.Resolved && std::fabs(Returned.Resolve().RedCoordinate - 0.5) < 1.0e-9,
           "[-] within the declared Bounded guarantee");

    Report("A projection into the same space is untouched",
           Slate::Project(Neutral, Working, Working).Resolve().RedCoordinate == 0.5,
           "[-] a conversion that does nothing perturbs nothing");

    Report("The working transfer is linear",
           Slate::Encode(Working, 0.25) == 0.25 && Slate::Decode(Working, 0.25) == 0.25,
           "[-] every computation above `66` ⑧ is linear");

    Report("The display transfer is not",
           Slate::Encode(Display, 0.25) != 0.25,
           "[-] and is applied exactly once, in `66`");

    // 📝 Negative coordinates arise legitimately: a working space wider than the display produces them. Clamping
    //    at the transfer would lose the sign before `66` had projected it.
    Report("A negative coordinate keeps its sign",
           Slate::Encode(Display, -0.25) < 0.0,
           "[-] transferred by odd reflection, never clamped");

    Report("A declared temperature projects",
           Slate::ProjectTemperature(5600.0, Working).Resolved,
           "[-] `36` §5's illuminant colour");

    Report("A temperature outside the locus refuses",
           !Slate::ProjectTemperature(100.0, Working).Resolved,
           "[-] refuses rather than extrapolating");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PROPERTIES
//------------------------------------------------------------------------------------------------------------------------

void VerifyProperties()
{
    std::printf("PropertySpecification\n");

    Slate::PropertyIndex Properties;

    // 📝 A roughness channel: bounded, and defaulted to something that is not zero. `42` §2's rule is that an
    //    absent value resolves to a declared default — a magnitude defaulted to zero produces a mirror.
    Slate::PropertyDeclaration Roughness;
    Roughness.Identity                 = "Roughness";
    Roughness.Current               = "Roughness";
    Roughness.Measured                = Slate::PropertyMeasure::Magnitude;
    Roughness.LowerMagnitude          = 0.0;
    Roughness.UpperMagnitude          = 1.0;
    Roughness.BoundsDeclared          = true;
    Roughness.Defaulted.Measured      = Slate::PropertyMeasure::Magnitude;
    Roughness.Defaulted.MagnitudeHeld = 0.5;

    Report("A declaration is accepted", Properties.Declare(Roughness).Resolved, "[-] its default validated");

    Report("An absent value reads its declared default",
           Properties.Resolve("Roughness").Resolve().MagnitudeHeld == 0.5,
           "[-] never assumed to be zero");

    // 🔴 `10` §2.2: a declaration whose own default is out of bounds presents an invalid value on every owner
    //    that never wrote it, which is every owner at the moment it arrives.
    Slate::PropertyDeclaration Impossible = Roughness;
    Impossible.Identity                   = "Impossible";
    Impossible.Defaulted.MagnitudeHeld    = 2.0;

    Report("A declaration with an invalid default is rejected",
           !Properties.Declare(Impossible).Resolved,
           "[-] validated at declaration");

    Slate::PropertyValue Offered;
    Offered.Measured      = Slate::PropertyMeasure::Magnitude;
    Offered.MagnitudeHeld = 0.25;

    Report("A valid write lands",
           Properties.Write("Roughness", Offered).Resolved
        && Properties.Resolve("Roughness").Resolve().MagnitudeHeld == 0.25,
           "[-] and is marked written");

    Slate::PropertyValue Exceeding;
    Exceeding.Measured      = Slate::PropertyMeasure::Magnitude;
    Exceeding.MagnitudeHeld = 4.0;

    Report("A write beyond the interval is rejected",
           !Properties.Write("Roughness", Exceeding).Resolved,
           "[-] refuses; it never bounds silently");

    Report("A rejected write leaves the prior value",
           Properties.Resolve("Roughness").Resolve().MagnitudeHeld == 0.25,
           "[-] no partial state");

    Report("Bounding is offered apart from writing",
           Slate::Bounded(Roughness, Exceeding).Resolve().MagnitudeHeld == 1.0,
           "[-] the presenter bounds, then writes");

    Slate::PropertyValue MismeasuredValue;
    MismeasuredValue.Measured    = Slate::PropertyMeasure::Truth;
    MismeasuredValue.TruthDeclared = true;

    Report("A value of the wrong measure is rejected",
           !Properties.Write("Roughness", MismeasuredValue).Resolved,
           "[-] the measure discriminates first");

    // 🔴 `36` §1: a colour without its space is rejected rather than assumed to be in the working space.
    Slate::PropertyDeclaration Albedo;
    Albedo.Identity                = "AlbedoColour";
    Albedo.Measured                = Slate::PropertyMeasure::Colour;
    Albedo.RequiredSpace           = Slate::WorkingSpaceIdentity;
    Albedo.Defaulted.Measured      = Slate::PropertyMeasure::Colour;
    Albedo.Defaulted.ColourHeld.RedCoordinate   = 0.5;
    Albedo.Defaulted.ColourHeld.GreenCoordinate = 0.5;
    Albedo.Defaulted.ColourHeld.BlueCoordinate  = 0.5;
    Albedo.Defaulted.ColourHeld.SpaceIdentity   = Slate::WorkingSpaceIdentity;

    Discard(Properties.Declare(Albedo));

    Slate::PropertyValue Spaceless;
    Spaceless.Measured                  = Slate::PropertyMeasure::Colour;
    Spaceless.ColourHeld.RedCoordinate  = 1.0;

    Report("A colour with no space is rejected",
           !Properties.Write("AlbedoColour", Spaceless).Resolved,
           "[-] no bare triple is ever held");

    Slate::PropertyValue WrongSpace = Spaceless;
    WrongSpace.ColourHeld.SpaceIdentity = Slate::DisplaySpaceIdentity;

    Report("A colour in the wrong space is rejected",
           !Properties.Write("AlbedoColour", WrongSpace).Resolved,
           "[-] the required space is stated, not assumed");

    Report("An undeclared property refuses",
           !Properties.Write("Absent", Offered).Resolved
        && !Properties.Resolve("Absent").Resolved,
           "[-] nothing declares it");

    Discard(Properties.Reclaim("Roughness"));

    Report("Reclamation restores the default",
           Properties.Resolve("Roughness").Resolve().MagnitudeHeld == 0.5
        && !Properties.ValueWritten("Roughness"),
           "[-] and clears the written mark");

    Report("Every held value satisfies its declaration",
           Properties.ValuesValid(),
           "[-] structurally, because Write is the only writer");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       DOCUMENT
//------------------------------------------------------------------------------------------------------------------------

void VerifyDocument()
{
    std::printf("SlateDocument\n");

    Slate::PopulationIndex Population;

    const Slate::Deliver<Slate::OwnerIdentity> FirstRegistration = Population.Register();

    Report("PopulationIndex registers", FirstRegistration.Resolved, "[-] an identity was registered");

    if (!FirstRegistration.Resolved)
        return;

    const Slate::OwnerIdentity Registered = FirstRegistration.Resolve();

    Report("Registered identity resolves",
           Population.Resolve(Registered),
           "[-] the generation still occupies the slot");

    Discard(Population.Withdraw(Registered));

    Report("Withdrawal advances the generation",
           !Population.Resolve(Registered),
           "[-] the prior generation reads absent");

    // 📝 🔴 The reused slot must not issue an identity equal to the withdrawn one. This is the property the
    //    whole generational scheme exists for, so it is measured rather than assumed.
    const Slate::Deliver<Slate::OwnerIdentity> SecondRegistration = Population.Register();

    Report("A reused slot issues a new generation",
           SecondRegistration.Resolved && SecondRegistration.Resolve() != Registered,
           "[-] the slot returned, the identity did not");

    Report("A stale identity survives reuse",
           !Population.Resolve(Registered),
           "[-] resolves absent, never to the new owner");

    Slate::RevisionSequence Revisions;

    Report("An open transaction is absent",
           Revisions.Open("", "TextureStroke").Resolved && Revisions.Committed().empty(),
           "[-] nothing enters the sequence until Seal");

    Report("A second open is rejected",
           !Revisions.Open("", "TextureStroke").Resolved,
           "[-] one transaction is open at a time");

    Discard(Revisions.Seal(1000000000ull, false));

    Report("A sealed transaction enters", Revisions.Committed().size() == 1u, "[-] exactly one");

    Report("Retreat scrubs backwards",
           Revisions.Retreat().Resolved && Revisions.ScrubPosition() == 0u,
           "[-] the position moved, the transaction remains");

    Report("Retreat at the beginning refuses",
           !Revisions.Retreat().Resolved,
           "[-] refuses rather than underflowing");

    // 📝 A heading at the current version resolves with zero migration steps. A later version is
    //    unmigratable rather than best-effort: `10` refuses a stream a later build wrote.
    Slate::StreamHeading CurrentHeading;
    CurrentHeading.Signature     = 0x45544C53u;
    CurrentHeading.StreamVersion = Slate::CurrentStreamVersion;

    const Slate::Deliver<std::uint32_t> Resolved = Slate::ResolveMigration(CurrentHeading);

    Report("A current stream needs no migration",
           Resolved.Resolved && Resolved.Resolve() == 0u,
           "[-] zero declared steps");

    Slate::StreamHeading LaterHeading = CurrentHeading;
    LaterHeading.StreamVersion        = Slate::CurrentStreamVersion + 1u;

    const Slate::Deliver<std::uint32_t> RejectedVersion = Slate::ResolveMigration(LaterHeading);

    Report("A later stream is rejected",
           !RejectedVersion.Resolved
        && RejectedVersion.Error.DeclaredReason == Slate::RefusalReason::VersionUnmigratable,
           "[-] the refusal carries its reason");

    Slate::StreamHeading ForeignHeading = CurrentHeading;
    ForeignHeading.Signature            = 0u;

    Report("A foreign stream is rejected",
           !Slate::ResolveMigration(ForeignHeading).Resolved,
           "[-] the signature gates the whole codec");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DEVICE
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 No instance is constructed here. This host runs where no vendor loader need be present, so it
//    verifies what `08` derives — the ordering — and leaves device construction to the windowed host.
void VerifySchedule()
{
    std::printf("SlateVulkan\n");

    Slate::RenderSchedule Schedule;

    Slate::DeclaredRecording VisibilityRecording;
    VisibilityRecording.Identity = "VisibilityRecording";
    VisibilityRecording.Produces = { Slate::SharedTarget::DepthSurface, Slate::SharedTarget::VisibilityIndex };
    VisibilityRecording.Command  = Slate::RecordingCommand::GraphicsRecording;

    Slate::DeclaredRecording RadianceRecording;
    RadianceRecording.Identity = "RadianceRecording";
    RadianceRecording.Reads    = { Slate::SharedTarget::DepthSurface, Slate::SharedTarget::VisibilityIndex };
    RadianceRecording.Produces = { Slate::SharedTarget::RadianceSurface };
    RadianceRecording.Command  = Slate::RecordingCommand::ComputeDispatch;

    Slate::DeclaredRecording ToneRecording;
    ToneRecording.Identity = "ToneRecording";
    ToneRecording.Reads    = { Slate::SharedTarget::RadianceSurface };
    ToneRecording.Produces = { Slate::SharedTarget::DisplaySurface };
    ToneRecording.Command  = Slate::RecordingCommand::ComputeDispatch;

    Slate::DeclaredRecording OverlayRecording;
    OverlayRecording.Identity        = "OverlayRecording";
    OverlayRecording.Amends          = { Slate::SharedTarget::DisplaySurface };
    OverlayRecording.Command         = Slate::RecordingCommand::GraphicsRecording;
    OverlayRecording.DisplayReferred = true;

    // 📝 Contributed in reverse deliberately. The ordering is derived from the declared reads and writes;
    //    contributing in the order the recordings run would prove nothing about the derivation.
    Report("Display-referred contribution accepted",
           Schedule.Contribute(OverlayRecording).Resolved,
           "[-] recorded after the tone line");

    Report("Tone contribution accepted",
           Schedule.Contribute(ToneRecording).Resolved,
           "[-] the tone line itself");

    Report("Radiance contribution accepted",
           Schedule.Contribute(RadianceRecording).Resolved,
           "[-] scene-referred");

    Report("Visibility contribution accepted",
           Schedule.Contribute(VisibilityRecording).Resolved,
           "[-] scene-referred");

    Slate::DeclaredRecording DuplicateProducer;
    DuplicateProducer.Identity = "SecondDepthRecording";
    DuplicateProducer.Produces = { Slate::SharedTarget::DepthSurface };

    Report("A second producer is rejected",
           !Schedule.Contribute(DuplicateProducer).Resolved,
           "[-] one producing recording per target");

    Slate::DeclaredRecording UngovernedRecording;
    UngovernedRecording.Identity           = "ComputeRasterRecording";
    UngovernedRecording.CapabilityRequired = true;

    Report("A capability with no substitution is rejected",
           !Schedule.Contribute(UngovernedRecording).Resolved,
           "[-] the substitution belongs to the contributor");

    Report("Ordering fixed", Schedule.Fix().Resolved, "[-] derived, never authored");

    const std::vector<Slate::DeclaredRecording>& Ordered = Schedule.Ordered();

    Report("Every contribution ordered", Ordered.size() == 4u, "[-] none silently dropped");

    if (Ordered.size() == 4u)
    {
        Report("Producers precede their consumers",
               std::strcmp(Ordered[0].Identity, "VisibilityRecording") == 0
            && std::strcmp(Ordered[1].Identity, "RadianceRecording")   == 0
            && std::strcmp(Ordered[2].Identity, "ToneRecording")       == 0,
               "[-] visibility, radiance, tone");

        Report("Display-referred ordered last",
               Ordered[3].DisplayReferred,
               "[-] nothing scene-referred follows the tone line");
    }

    Report("A second Fix is rejected", !Schedule.Fix().Resolved, "[-] the ordering is immutable");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PARITY
//------------------------------------------------------------------------------------------------------------------------

void VerifyParity()
{
    std::printf("SlateCompute\n");

    Slate::ParityRunner Runner;

    Slate::ParityRegistration OrientationEntry;
    OrientationEntry.EntryName = "ClassifyOrientation";
    OrientationEntry.Reserved   = Slate::PrecisionGuarantee::Exact;

    Report("Registration accepted", Runner.Register(OrientationEntry).Resolved, "[-] one entry point");

    Report("A duplicate registration is rejected",
           !Runner.Register(OrientationEntry).Resolved,
           "[-] one registration per spelling");

    // 🔴 `32` §4.1 stage D checks at build time that every `Shared/` entry point is registered. Registering
    //    them here is the run-time half of the same statement: an entry point that is registered and never
    //    compared reports zero samples and withdraws the agreement, which the vacancy check below relies on.
    const char* const ExactEntryNames[] =
    {
        "ClassifyIncircle",
        "ClassifySegmentIntersection",
        "ClassifyIntervalContainment",
        // 🔴 `02` §5 places `LatticeProjection` at Tier A and gives the reason `54` §2 repeats from the
        //    consuming side: `82` classifies a position on the host and `70` classifies it on the device.
        //    Registered as Exact rather than Bounded because both halves of the classification are — the
        //    unskewing is one correctly-rounded division per axis and the flooring is integral.
        "ClassifyLatticeCell",
        "ProjectPlanarSample",
        "ProjectSubPixelOffset",
        "TransmissionPrecedes",
        "ProjectAccumulationWeight"
    };

    bool ExactRegistrationsHeld = true;

    for (const char* const EntryName : ExactEntryNames)
    {
        Slate::ParityRegistration Registering;
        Registering.EntryName = EntryName;
        Registering.Reserved   = Slate::PrecisionGuarantee::Exact;

        if (!Runner.Register(Registering).Resolved)
            ExactRegistrationsHeld = false;
    }

    Report("Every exact entry point registers",
           ExactRegistrationsHeld,
           "[-] eight beside the orientation predicate");

    Slate::ParityRegistration SphericalEntry;
    SphericalEntry.EntryName = "ProjectSphericalSample";
    SphericalEntry.Reserved   = Slate::PrecisionGuarantee::Bounded;

    Report("A bounded entry point registers",
           Runner.Register(SphericalEntry).Resolved,
           "[-] compared against a bound, never for equality");

    // 🔴 `28` §2's three surfaces are baked on the host and sampled on the device through the same two mappings,
    //    so both of them cross the toolchain seam in both directions and both are covered here. Neither is
    //    compared against a second implementation of itself: ① is compared as an inversion of its own parameter
    //    routine, and ③ against the unit length every consumer of its direction assumes.
    const char* const AtmosphereEntryNames[] =
    {
        "ProjectTransmittanceCoordinate",
        "ProjectSkyViewDirection"
    };

    bool AtmosphereRegistrationsHeld = true;

    for (const char* const EntryName : AtmosphereEntryNames)
    {
        Slate::ParityRegistration Registering;
        Registering.EntryName = EntryName;
        Registering.Reserved   = Slate::PrecisionGuarantee::Bounded;

        if (!Runner.Register(Registering).Resolved)
            AtmosphereRegistrationsHeld = false;
    }

    Report("Every atmosphere mapping registers",
           AtmosphereRegistrationsHeld,
           "[-] both of the mappings that cross the seam in both directions");

    // 🔴 `30` §1's composite and `66` §3's compression are both measured against a bound rather than compared
    //    for equality, because each carries a division whose last place differs between two toolchains that
    //    reassociate. Registering them here is what makes their comparison arms run at all — an arm that
    //    exists and is never registered is uncompared source wearing the appearance of a proof.
    const char* const BoundedEntryNames[] =
    {
        "ResolveExactComposite",
        "ProjectToneCompressed"
    };

    bool BoundedRegistrationsHeld = true;

    for (const char* const EntryName : BoundedEntryNames)
    {
        Slate::ParityRegistration Registering;
        Registering.EntryName = EntryName;
        Registering.Reserved   = Slate::PrecisionGuarantee::Bounded;

        if (!Runner.Register(Registering).Resolved)
            BoundedRegistrationsHeld = false;
    }

    Report("Every bounded projection registers",
           BoundedRegistrationsHeld,
           "[-] the composite `30` resolves and the compression `66` applies");

    const std::vector<Slate::ParityReport>& Reports = Runner.Compare();

    Report("One report per registration", Reports.size() == 14u, "[-] in registration order");

    if (!Reports.empty())
    {
        Report("Samples were compared",
               Reports[0].SampleCount > 0u,
               "[-] an empty sample set never holds");

        Report("Orientation antisymmetry holds",
               Reports[0].DisagreeingCount == 0u,
               "[-] reversing two operands negates the sign, exactly");
    }

    bool EverySampleSetCompared = true;

    for (const Slate::ParityReport& Held : Reports)
    {
        if (Held.SampleCount == 0u || Held.DisagreeingCount != 0u)
            EverySampleSetCompared = false;
    }

    Report("Every registration was genuinely compared",
           EverySampleSetCompared,
           "[-] none passed on an empty sample set");

    // 📝 Every report is measured rather than the tail from a counted position. An exact arm compares bit for
    //    bit and leaves its deviation at nothing, so the bound holds over it trivially — and the check no
    //    longer has to be corrected each time a registration is added ahead of the bounded ones.
    bool EveryBoundHeld = Reports.size() == 14u;

    for (const Slate::ParityReport& Held : Reports)
    {
        if (Held.LargestDeviation > Slate::SampleUnitPlaceLimit)
            EveryBoundHeld = false;
    }

    Report("Every bounded entry point stays inside its bound",
           EveryBoundHeld,
           "[-] measured in units in the last place");

    Report("Agreement declared", Runner.AgreementHeld(), "[-] every registered entry point held");

    // 🚧 An unregistered comparison must not pass vacantly. Registering an entry point with no declared
    //    comparison reports zero samples and withdraws the agreement, rather than reporting a proof nobody
    //    produced. This is the gate that would otherwise let `Shared/` grow uncovered entry points.
    Slate::ParityRunner VacantRunner;

    Slate::ParityRegistration UncomparedEntry;
    // 📝 `26`'s outline coverage is unbuilt, so it is the honest choice of a name that is legitimately
    //    uncompared. It was `IntegrateQuadrature` until that component landed, which is exactly the churn the
    //    check is meant to force: a vacancy check naming a built entry point has stopped checking anything.
    UncomparedEntry.EntryName = "ProjectOutlineCoverage";
    UncomparedEntry.Reserved   = Slate::PrecisionGuarantee::Bounded;

    Discard(VacantRunner.Register(UncomparedEntry));
    VacantRunner.Compare();

    Report("An uncompared entry point does not hold",
           !VacantRunner.AgreementHeld(),
           "[-] zero samples is not agreement");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ATMOSPHERE
//------------------------------------------------------------------------------------------------------------------------

// 📝 `28` is verified as its own unit because it is the compute product whose surfaces are baked on the host and
//    sampled on the device, and the rebuild discipline it verifies is what the schedule's contributor reads.
void VerifyAtmosphere()
{
    std::printf("AtmosphereIntegrator\n");

    // 📝 `02` §5's Gauss–Legendre rule, derived on the recurrence: every optical depth in `28` integrates against
    //    it, so a rule that will not derive invalidates each surface rather than one.
    Slate::QuadratureRule Rule;
    Report("A rule derives", Rule.Derive(32u).Resolved, "[-] Gauss–Legendre, Newton on the recurrence");

    Slate::AtmosphereIntegrator Atmosphere;

    Report("Nothing rebuilds before a medium is declared",
           !Atmosphere.Rebuild(Slate::DeclaredWorkingSpace(), Rule).Resolved,
           "[-] the refusal is the guarantee, not a silence");

    Slate::MediumSpecification Earth;

    Report("Earth's medium validates",
           Atmosphere.DeclareMedium(Earth).Resolved,
           "[-] the defaults");

    Slate::MediumSpecification Sizeless = Earth;
    Sizeless.OzoneHalfWidth = 0.0;

    Report("An ozone tent of no width is rejected",
           !Atmosphere.DeclareMedium(Sizeless).Resolved,
           "[-] the tent must stand somewhere");

    Discard(Atmosphere.DeclareSun(0.0, 0.3, -0.95));
    Discard(Atmosphere.DeclareCameraAltitude(1000.0));

    Report("A rebuild is owed",
           Atmosphere.RebuildOwed(),
           "[-] all three surfaces");

    Report("The rebuild delivers",
           Atmosphere.Rebuild(Slate::DeclaredWorkingSpace(), Rule).Resolved && !Atmosphere.RebuildOwed(),
           "[-] ① ② ③, in order");

    Report("The three surfaces total the declared extent",
           Atmosphere.ResidentBytes() == Slate::AtmosphereResidentBytes,
           "[B] 298 KiB");

    Report("An unchanged medium rebuilds nothing",
           Atmosphere.Rebuild(Slate::DeclaredWorkingSpace(), Rule).Resolved
        && Atmosphere.MediumRebuildCount() == 1u,
           "[-] the count is the proof, not the words");

    Discard(Atmosphere.DeclareSun(0.0, 0.3000001, -0.95));

    Report("An immaterial sun move rebuilds nothing",
           !Atmosphere.RebuildOwed(),
           "[-] below `SunDirectionMateriality`");

    Discard(Atmosphere.DeclareSun(0.0, 1.0, 0.0));

    Report("A material sun move owes ③ alone",
           Atmosphere.RebuildOwed()
        && Atmosphere.Rebuild(Slate::DeclaredWorkingSpace(), Rule).Resolved
        && Atmosphere.MediumRebuildCount() == 1u
        && Atmosphere.SkyViewRebuildCount() == 2u,
           "[-] the medium stays integrated");

    const double Seam = Slate::Pi - 1.0e-4;

    double BeforeRed   = 0.0;
    double BeforeGreen = 0.0;
    double BeforeBlue  = 0.0;
    double AfterRed    = 0.0;
    double AfterGreen  = 0.0;
    double AfterBlue   = 0.0;

    Discard(Atmosphere.SampleSkyView(std::cos(Seam), 0.1, std::sin(Seam), BeforeRed, BeforeGreen, BeforeBlue));
    Discard(Atmosphere.SampleSkyView(std::cos(-Seam), 0.1, std::sin(-Seam), AfterRed, AfterGreen, AfterBlue));

    Report("The azimuth wrap carries no seam",
           std::fabs(BeforeRed - AfterRed) < 1.0e-3,
           "[-] texel 191 blends with texel 0");

    double OccludedRed   = 0.0;
    double OccludedGreen = 0.0;
    double OccludedBlue  = 0.0;

    Discard(Atmosphere.SampleTransmittance(0.0, -1.0, OccludedRed, OccludedGreen, OccludedBlue));

    Report("The sun below the horizon is occluded",
           OccludedRed == 0.0,
           "[-] a zero write, not an underflow");

    double ZenithRed   = 0.0;
    double ZenithGreen = 0.0;
    double ZenithBlue  = 0.0;

    Discard(Atmosphere.SampleTransmittance(0.0, 1.0, ZenithRed, ZenithGreen, ZenithBlue));

    std::printf("  🔍 zenith %.9f %.9f %.9f | rayleigh %.6e %.6e %.6e | ozone %.6e %.6e %.6e | mie %.6e\n",
                ZenithRed, ZenithGreen, ZenithBlue,
                Atmosphere.Coefficient().RayleighScattering[0],
                Atmosphere.Coefficient().RayleighScattering[1],
                Atmosphere.Coefficient().RayleighScattering[2],
                Atmosphere.Coefficient().OzoneAbsorption[0],
                Atmosphere.Coefficient().OzoneAbsorption[1],
                Atmosphere.Coefficient().OzoneAbsorption[2],
                Atmosphere.Coefficient().MieExtinction);

    Report("A vertical path extinguishes blue hardest",
           ZenithRed > ZenithGreen && ZenithGreen > ZenithBlue && ZenithRed < 1.0,
           "[-] Rayleigh across the whole depth");

    double AmbientRed   = 0.0;
    double AmbientGreen = 0.0;
    double AmbientBlue  = 0.0;

    Atmosphere.Irradiance().Evaluate(0.0, 1.0, 0.0, AmbientRed, AmbientGreen, AmbientBlue);

    Report("The irradiance is never negative",
           AmbientRed >= 0.0 && AmbientGreen >= 0.0 && AmbientBlue >= 0.0,
           "[-] the reconstruction clamps at zero");

    Atmosphere.DeclareAtmospherePresence(false);

    Slate::ColourSpecification Floor;
    Floor.RedCoordinate = 0.02;
    Floor.SpaceIdentity = Slate::WorkingSpaceIdentity;

    Discard(Atmosphere.DeclareConstantFloor(Floor));

    double FloorRed   = 0.0;
    double FloorGreen = 0.0;
    double FloorBlue  = 0.0;

    Discard(Atmosphere.SampleSkyView(0.0, 1.0, 0.0, FloorRed, FloorGreen, FloorBlue));

    Report("A disabled atmosphere resolves to the floor",
           FloorRed == 0.02,
           "[-] `18` §5 and `30` §3 fall back to the same");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 TOPOLOGY AND MATERIALS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Two unit squares sharing an edge, the second declared with a duplicated position rather than a shared
//    vertex — which is what a format storing a coordinate per corner produces at every seam. Welding is the only
//    thing that can see through it, so it is the shape the check is built on.
void VerifyTopology()
{
    std::printf("TopologyConditioning\n");

    Slate::TopologyStructure Imported;

    std::vector<Slate::DocumentPosition> Positions(6);
    Positions[0].PositionX = 0.0;  Positions[0].PositionY = 0.0;
    Positions[1].PositionX = 1.0;  Positions[1].PositionY = 0.0;
    Positions[2].PositionX = 1.0;  Positions[2].PositionY = 1.0;
    Positions[3].PositionX = 0.0;  Positions[3].PositionY = 1.0;
    Positions[4].PositionX = 1.0;  Positions[4].PositionY = 0.0;   // duplicate of vertex 1
    Positions[5].PositionX = 2.0;  Positions[5].PositionY = 0.0;

    Discard(Imported.DeclarePositions(Positions));

    Report("A run of two corners is rejected",
           !Imported.DeclareFace({ 0u, 1u }).Resolved,
           "[-] fewer than three corners is not a face");

    Discard(Imported.DeclareFace({ 0u, 1u, 2u, 3u }));
    Discard(Imported.DeclareFace({ 4u, 5u, 2u }));

    Report("An unsealed topology refuses conditioning",
           !Slate::TopologyConditioning{}.Condition(Imported).Resolved,
           "[-] not immutable for the run");

    Report("Sealing advances the revision",
           Imported.Seal().Resolved && Imported.Revision() == 1u,
           "[-] what `24` §3 keys on");

    Report("A declaration after the seal is rejected",
           !Imported.DeclareFace({ 0u, 1u, 2u }).Resolved,
           "[-] the arrays are stable from here");

    Report("An absent enrollment defaults to one material",
           Imported.MaterialRegistration().size() == Imported.FaceCount(),
           "[-] `50` §3's last-resort row");

    Slate::TopologyConditioning Conditioned;

    Report("Conditioning derives", Conditioned.Condition(Imported).Resolved, "[-] beside, never into");

    Report("Coincident vertices weld to one position",
           Conditioned.WeldedCount() == 5u,
           "[-] six imported vertices, five positions");

    Report("The imported arrays are untouched",
           Imported.VertexCount() == 6u && Imported.FaceCount() == 2u,
           "[-] an index means the same thing after as before");

    Report("A degenerate face is registered, not removed",
           Imported.FaceCount() == 2u,
           "[-] `38` §3 excludes; it never renumbers");

    // 📐 Every extent is rounded outward, so the whole extent strictly contains every position it was built from.
    const Slate::ConditionedExtent Whole = Conditioned.TopologyExtent();

    Report("Extents are conservative outward",
           Whole.Minimum.PositionX < 0.0 && Whole.Maximum.PositionX > 2.0,
           "[mm] never inward — `38` §6");

    Report("Perpendiculars were derived",
           Conditioned.Perpendiculars().size() == Imported.VertexCount(),
           "[-] one per imported vertex");

    // 🔴 With no domain coordinates there is no domain, so the basis is absent rather than substituted — `18`
    //    §1.1. An orthonormalised substitute would be a fabricated value, which `24` §2 rejects for transfer.
    bool BasesAbsent = !Conditioned.TangentBases().empty();

    for (const Slate::TangentBasis& Held : Conditioned.TangentBases())
    {
        if (Held.BasisDeclared)
            BasesAbsent = false;
    }

    Report("Absent coordinates leave the basis absent",
           BasesAbsent && !Conditioned.TangentBasesRetained(),
           "[-] never a fabricated substitute");

    Report("A boundary edge yields no adjacency",
           !Conditioned.AdjacentCorner(0u).Resolved,
           "[-] refuses rather than choosing one arbitrarily");
}

void VerifyMaterials()
{
    std::printf("MaterialSpecification\n");

    Slate::MaterialIndex Materials;

    const Slate::Deliver<std::uint32_t> Declared = Materials.Declare("Textured metal");

    Report("A material is declared", Declared.Resolved, "[-] addressed by identity");

    if (!Declared.Resolved)
        return;

    Slate::MaterialSpecification* Amending = Materials.Amend(Declared.Resolve()).Resolve();

    Report("An undeclared material refuses",
           !Materials.Resolve(Declared.Resolve() + 1u).Resolved,
           "[-] no such material");

    // 🔴 An absent channel resolves to its declared default, which is not zero. A transmission channel defaulted
    //    to zero is opaque and an occlusion channel defaulted to zero is black; only one of those is right.
    Slate::ChannelSpecification Occlusion;
    Occlusion.Source         = Slate::ChannelSource::Absent;
    Occlusion.Measured       = Slate::ChannelMeasure::Scalar;
    Occlusion.DefaultScalar  = 1.0;
    Occlusion.LowerMagnitude = 0.0;
    Occlusion.UpperMagnitude = 1.0;

    Report("An absent channel declares a non-zero default",
           Amending->DeclareChannel(Slate::ChannelSubject::AmbientOcclusion, Occlusion).Resolved
        && Amending->Channel(Slate::ChannelSubject::AmbientOcclusion).DefaultScalar == 1.0,
           "[-] fully unoccluded, not black");

    Slate::ChannelSpecification Impossible = Occlusion;
    Impossible.DefaultScalar               = 4.0;

    Report("A default outside its interval is rejected",
           !Amending->DeclareChannel(Slate::ChannelSubject::Roughness, Impossible).Resolved,
           "[-] validated at declaration");

    Slate::ChannelSpecification Albedo;
    Albedo.Source   = Slate::ChannelSource::Constant;
    Albedo.Measured = Slate::ChannelMeasure::Reflectance;

    Report("A colour channel with no space is rejected",
           !Amending->DeclareChannel(Slate::ChannelSubject::AlbedoColour, Albedo).Resolved,
           "[-] `36` §1: no bare triple");

    Albedo.ConstantColour.SpaceIdentity = Slate::WorkingSpaceIdentity;
    Albedo.DefaultColour.SpaceIdentity  = Slate::WorkingSpaceIdentity;

    Report("A colour channel with its space is accepted",
           Amending->DeclareChannel(Slate::ChannelSubject::AlbedoColour, Albedo).Resolved,
           "[-] the coordinate carries its space");

    Slate::ChannelSpecification Sheen = Occlusion;
    Sheen.Source = Slate::ChannelSource::Layered;

    Discard(Amending->DeclareChannel(Slate::ChannelSubject::SheenRoughness, Sheen));
    Amending->DeclareReflectance(Slate::ReflectanceSelection::Standard);

    Report("An unconsumed channel is not sampled",
           !Amending->ChannelSampled(Slate::ChannelSubject::SheenRoughness),
           "[-] `18` §9: unread channels are not sampled");

    Amending->DeclareReflectance(Slate::ReflectanceSelection::Cloth);

    Report("A retained channel returns with its selection",
           Amending->ChannelSampled(Slate::ChannelSubject::SheenRoughness),
           "[-] `42` §5: never discarded on switch");

    Report("Colour conversion reads the declared measure",
           Amending->ChannelConverted(Slate::ChannelSubject::AlbedoColour)
       && !Amending->ChannelConverted(Slate::ChannelSubject::AmbientOcclusion),
           "[-] never inferred from a name");

    Slate::PartitionResolutionIndex Resolutions;

    Slate::ResolvedPartition Resolving;
    Resolving.Owner.SlotIndex    = 7u;
    Resolving.Owner.SlotGeneration = 1u;
    Resolving.MaterialIndex         = Declared.Resolve();
    Resolving.FaceCount               = 96u;

    const Slate::Deliver<Slate::PartitionIdentity> Registered = Resolutions.Declare(Resolving);

    Report("A partition resolves to an owner",
           Registered.Resolved
        && Resolutions.Resolve(Registered.Resolve()).Resolve().Owner == Resolving.Owner,
           "[-] `00` §10 conflict 15, closed");

    Report("A partition with no owner is rejected",
           !Resolutions.Declare(Slate::ResolvedPartition{}).Resolved,
           "[-] the resolution is derived, never authored");

    Resolutions.Reclaim();

    Report("A rebuild staleness the prior identity",
           !Resolutions.Resolve(Registered.Resolve()).Resolved,
           "[-] refuses rather than resolving to whoever took the ordinal");
}

void VerifyVector()
{
    std::printf("VectorInterchange\n");

    // 📝 One unit square as four line segments. Flattened, classified, and then classified again at a coarser
    //    tolerance — the containment must not depend on the tolerance for a path with no curvature.
    Slate::OutlineSpecification Square;

    Slate::OutlinePath Path;
    Path.Origin.PositionX = 0.0;
    Path.Origin.PositionY = 0.0;
    Path.ClosedRun        = true;

    const double CornerX[4] = { 1.0, 1.0, 0.0, 0.0 };
    const double CornerY[4] = { 0.0, 1.0, 1.0, 0.0 };

    for (std::uint32_t Index = 0u; Index < 4u; ++Index)
    {
        Slate::PathSegment Segment;
        Segment.Subject             = Slate::SegmentSubject::Line;
        Segment.Terminus.PositionX  = CornerX[Index];
        Segment.Terminus.PositionY  = CornerY[Index];

        Path.Segments.push_back(Segment);
    }

    Square.Paths.push_back(Path);

    Slate::VectorInterchange Outline;

    Report("An empty source is rejected",
           !Outline.DeclareFromFile(Slate::OutlineSpecification{}, "empty.vector").Resolved,
           "[-] no path was declared");

    Report("A supplied-text source retains its text",
           Outline.DeclareFromText(Square, "<square/>").Resolved && Outline.TextRetained(),
           "[-] nothing depends on a clipboard surviving");

    Report("A file source produces the same specification",
           Outline.DeclareFromFile(Square, "square.vector").Resolved
        && !Outline.TextRetained()
        && Outline.Declared().Paths.size() == 1u,
           "[-] indistinguishable downstream");

    const std::vector<std::vector<Slate::PlanarPosition>> Fine   = Outline.Flatten(1.0e-4);
    const std::vector<std::vector<Slate::PlanarPosition>> Coarse = Outline.Flatten(1.0e-1);

    Report("Interior classifies inside",
           Outline.Classify(Fine, 0.5, 0.5) > 0,
           "[-] the non-zero rule");

    Report("Exterior classifies outside",
           Outline.Classify(Fine, 1.5, 0.5) < 0,
           "[-] and the ray is unambiguous");

    // 🔴 A boundary resolves to zero and not to inside. `70` resolves coverage from this, and a boundary reported
    //    interior gives every outline a one-texel bias outward at its own edge.
    Report("A boundary classifies as boundary",
           Outline.Classify(Fine, 0.0, 0.5) == 0,
           "[-] never interior");

    // 📐 A position level with two shared vertices is the case a closed coordinate test counts twice. The half-open
    //    test is what makes it contribute exactly once, so the artist sees no hole at any vertex.
    Report("A position level with a vertex classifies once",
           Outline.Classify(Fine, 0.5, 0.0) == 0 && Outline.Classify(Fine, 0.5, 1.0) == 0,
           "[-] the half-open coordinate test");

    Report("Tolerance does not change containment of a straight path",
           Outline.Classify(Coarse, 0.5, 0.5) > 0,
           "[-] resolution-relative, not resolution-dependent");

    Slate::Refusal Declining;
    Declining.DeclaredReason = Slate::RefusalReason::ContentUnsupported;
    Declining.Detail         = "effect operations are outside the accepted subset";

    Outline.Refuse("feGaussianBlur", 412u, Declining);

    Report("A refusal names its construct and position",
           Outline.Refusals().size() == 1u
        && Outline.Refusals()[0].SourceIndex == 412u
        && !Outline.Refusals()[0].Construct.empty(),
           "[-] never bare 'unsupported'");

    // 📝 A stroke is converted at intake rather than stored as a width, so a placement that scales the source
    //    does not thin the stroke — `52` §2.
    const std::vector<Slate::PlanarPosition> Traversed = Slate::Flatten(Path.Origin, Path.Segments, 1.0e-4);

    Report("A stroke converts to an outline",
           Slate::OffsetOutline(Traversed, 0.05, true).Resolved,
           "[-] no stroke width is stored");

    Report("A stroke of no width is rejected",
           !Slate::OffsetOutline(Traversed, 0.0, true).Resolved,
           "[-] it encloses nothing");

    // 📐 An arc is flattened by sagitta, so a tighter tolerance produces strictly more positions. A flattening
    //    that ignored the tolerance would produce the same count either way.
    Slate::PathSegment Arc;
    Arc.Subject            = Slate::SegmentSubject::Arc;
    Arc.Terminus.PositionX = 2.0;
    Arc.RadiusX        = 1.0;
    Arc.RadiusY       = 1.0;
    Arc.SweepEnabled       = true;

    const std::size_t FineArc   = Slate::Flatten(Slate::PlanarPosition{}, { Arc }, 1.0e-4).size();
    const std::size_t CoarseArc = Slate::Flatten(Slate::PlanarPosition{}, { Arc }, 1.0e-1).size();

    Report("Arc flattening follows the tolerance",
           FineArc > CoarseArc && CoarseArc >= 2u,
           "[-] by sagitta, per level");

    Slate::TypefaceInterchange Typeface;
    Typeface.DeclareTypeface(1u, 1000.0);

    Slate::GlyphSpecification Glyph;
    Glyph.GlyphIdentity = 42u;
    Glyph.Advance       = 600.0;

    Report("A glyph is declared by identity",
           Typeface.DeclareGlyph(Glyph).Resolved
        && Typeface.ResolveGlyph(42u).Resolved,
           "[-] never by character");

    Report("A duplicate glyph is rejected",
           !Typeface.DeclareGlyph(Glyph).Resolved,
           "[-] one declaration per glyph");

    Typeface.DeclareAdjustment(42u, 43u, -30.0);

    Report("An undeclared pair adjusts by nothing",
           Typeface.Adjustment(42u, 43u) == -30.0 && Typeface.Adjustment(43u, 42u) == 0.0,
           "[-] and the pair is ordered");

    // 🔴 Text stores a glyph sequence **and** its characters. Storing only characters means replacing a typeface
    //    silently reshapes text the artist already positioned — `52` §3.
    Slate::ResolvedText Text;
    Text.GlyphSequence    = { 42u };
    Text.Characters       = "A";
    Text.TypefaceIdentity = 1u;

    Report("Text stores glyphs and characters both",
           !Text.GlyphSequence.empty() && !Text.Characters.empty(),
           "[-] never characters alone");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE CAMERA
//------------------------------------------------------------------------------------------------------------------------

void VerifyCamera()
{
    std::printf("CameraProjection\n");

    Slate::CameraSpecification Declaring;
    Declaring.Placement.Translation.PositionZ = 10.0;
    Declaring.SensorProportion                = 16.0 / 9.0;

    Slate::CameraSpecification Inverted    = Declaring;
    Inverted.Clipping.Nearest              = 100.0;
    Inverted.Clipping.Furthest             = 1.0;

    Report("An inverted clipping interval is rejected",
           !Slate::Derive(Inverted).Resolved && !Inverted.Clipping.IntervalValid(),
           "[-] a frustum with no interior is rejected, never derived");

    const Slate::Deliver<Slate::ViewProjection> Projected = Slate::Derive(Declaring);

    Report("A declared camera projects", Projected.Resolved, "[-] the composed matrix exists");

    if (!Projected.Resolved)
        return;

    // 📐 The depth convention, measured rather than assumed. A position on the nearest plane must resolve to
    //    NearPlaneDepth and one on the furthest to FarPlaneDepth; the forward arrangement inverts both.
    const double* Composed = Projected.Resolve().Composed.Coefficient;

    const double NearestClipDepth = Composed[10] * (-Declaring.Clipping.Nearest) + Composed[14];
    const double NearestClipScale = Composed[11] * (-Declaring.Clipping.Nearest) + Composed[15];

    const double FurthestClipDepth = Composed[10] * (-Declaring.Clipping.Furthest) + Composed[14];
    const double FurthestClipScale = Composed[11] * (-Declaring.Clipping.Furthest) + Composed[15];

    Report("The nearest plane resolves to one",
           NearestClipScale != 0.0
        && std::fabs(NearestClipDepth / NearestClipScale - Slate::NearPlaneDepth) < 1.0e-9,
           "[-] depth is reversed, repository-wide");

    Report("The furthest plane resolves to zero",
           FurthestClipScale != 0.0
        && std::fabs(FurthestClipDepth / FurthestClipScale - Slate::FarPlaneDepth) < 1.0e-9,
           "[-] and the two never disagree");

    Slate::CameraProjection Camera;

    Slate::OwnerIdentity CameraOwner;
    CameraOwner.SlotIndex    = 3u;
    CameraOwner.SlotGeneration = 1u;

    Report("A camera declares against an owner",
           Camera.Declare(CameraOwner, Declaring).Resolved && Camera.DerivationOwed(),
           "[-] and owes a reconciliation");

    Report("Reconciliation derives the frustum",
           Camera.Reconcile().Resolved && !Camera.DerivationOwed(),
           "[-] both derivations, one call");

    // 📐 The camera sits at ten along the third axis looking toward the origin, so an extent at the origin is
    //    inside and one far behind the camera is outside.
    Slate::DocumentPosition NearMinimum;
    NearMinimum.PositionX = -1.0;  NearMinimum.PositionY = -1.0;  NearMinimum.PositionZ = -1.0;

    Slate::DocumentPosition NearMaximum;
    NearMaximum.PositionX = 1.0;  NearMaximum.PositionY = 1.0;  NearMaximum.PositionZ = 1.0;

    Report("An extent before the camera is inside",
           Camera.Frustum().Classify(NearMinimum, NearMaximum) >= 0,
           "[-] the frustum contains what the camera sees");

    Slate::DocumentPosition BehindMinimum;
    BehindMinimum.PositionZ = 400.0;

    Slate::DocumentPosition BehindMaximum;
    BehindMaximum.PositionX = 1.0;  BehindMaximum.PositionY = 1.0;  BehindMaximum.PositionZ = 402.0;

    Report("An extent behind the camera is outside",
           Camera.Frustum().Classify(BehindMinimum, BehindMaximum) < 0,
           "[-] and is culled before anything reads it");

    // 🔴 The rebasing property, measured at a billion millimetres. An extent that far from the document origin
    //    classifies against the camera only because `02` §3.2's subtraction preceded the narrowing.
    Slate::CameraSpecification Distant = Declaring;
    Distant.Placement.Translation.PositionX = 1.0e9;
    Distant.Placement.Translation.PositionZ = 10.0;

    Slate::CameraProjection DistantCamera;
    Discard(DistantCamera.Declare(CameraOwner, Distant));
    Discard(DistantCamera.Reconcile());

    Slate::DocumentPosition DistantMinimum;
    DistantMinimum.PositionX = 1.0e9 - 1.0;  DistantMinimum.PositionY = -1.0;  DistantMinimum.PositionZ = -1.0;

    Slate::DocumentPosition DistantMaximum;
    DistantMaximum.PositionX = 1.0e9 + 1.0;  DistantMaximum.PositionY = 1.0;  DistantMaximum.PositionZ = 1.0;

    Report("Rebasing survives at a billion millimetres",
           DistantCamera.Frustum().Classify(DistantMinimum, DistantMaximum) >= 0,
           "[mm] the subtraction preceded the narrowing");

    // 📝 A navigation gesture Seals one transaction. Amending fifty times is one seal, not fifty.
    Slate::NavigationSequence Navigating;

    Report("A gesture opens", Navigating.Open(Slate::NavigationSubject::Orbit, Declaring).Resolved,
           "[-] holding the prior specification");

    Report("A second open is rejected",
           !Navigating.Open(Slate::NavigationSubject::Pan, Declaring).Resolved,
           "[-] one gesture at a time");

    for (std::uint32_t Index = 0u; Index < 50u; ++Index)
        Discard(Navigating.Amend(4.0, 0.0));

    const Slate::Deliver<Slate::CameraSpecification> Sealed = Navigating.Seal();

    Report("An orbit preserves the focus distance",
           Sealed.Resolved
        && std::fabs(std::sqrt(Sealed.Resolve().Placement.Translation.PositionX
                             * Sealed.Resolve().Placement.Translation.PositionX
                             + Sealed.Resolve().Placement.Translation.PositionZ
                             * Sealed.Resolve().Placement.Translation.PositionZ) - 10.0) < 1.0e-6,
           "[mm] fifty amendments, one seal, one distance");

    Report("A gesture Seals once", !Navigating.GestureOpen(), "[-] and nothing between Open and Seal recorded");

    Slate::NavigationSequence Abandoning;
    Discard(Abandoning.Open(Slate::NavigationSubject::Pan, Declaring));
    Discard(Abandoning.Amend(100.0, 100.0));

    const Slate::Deliver<Slate::CameraSpecification> Restored = Abandoning.Abandon();

    Report("Abandonment restores the prior camera",
           Restored.Resolved
        && Restored.Resolve().Placement.Translation.PositionZ == Declaring.Placement.Translation.PositionZ,
           "[-] no partial state");

    // 🔴 Framing changes the placement only. A framing that also changed the field would change the composition.
    const Slate::Deliver<Slate::DecomposedTransform> Framed = Slate::Frame(Declaring, NearMinimum, NearMaximum);

    Report("Framing produces a placement",
           Framed.Resolved,
           "[-] from an extent and a projection");

    Report("An inverted extent frames nothing",
           !Slate::Frame(Declaring, NearMaximum, NearMinimum).Resolved,
           "[-] refuses rather than framing a negative volume");

    Slate::CameraSpecification Zoomed = Declaring;
    Zoomed.ExtentParameter             = 20.0;

    const Slate::Deliver<Slate::DecomposedTransform> Narrower = Slate::Frame(Zoomed, NearMinimum, NearMaximum);

    Report("A narrower field frames from further back",
           Framed.Resolved && Narrower.Resolved
        && Narrower.Resolve().Translation.PositionZ > Framed.Resolve().Translation.PositionZ,
           "[mm] the placement moved; the field did not");

    Report("Exposure is the camera's",
           Camera.Exposure() == Declaring.Exposure,
           "[EV] stored in the document — `00` §10 conflict 33");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ILLUMINANTS
//------------------------------------------------------------------------------------------------------------------------

void VerifyIlluminants()
{
    std::printf("IlluminantPopulation\n");

    Slate::IlluminantPopulation Illuminants;

    Slate::OwnerIdentity FirstOwner;
    FirstOwner.SlotIndex    = 1u;
    FirstOwner.SlotGeneration = 1u;

    Slate::IlluminantSpecification Declaring;
    Declaring.Emission                     = Slate::EmissionShape::Point;
    Declaring.EmissionRadius               = 25.0;
    Declaring.ExtentReach                  = 500.0;
    Declaring.DeclaredColour.RedCoordinate = 1.0;
    Declaring.DeclaredColour.SpaceIdentity = Slate::WorkingSpaceIdentity;

    Report("An illuminant is declared",
           Illuminants.Declare(FirstOwner, Declaring).Resolved,
           "[-] against an registered owner");

    // 🔴 `44` §3: every emission shape has a non-zero size. A zero radius is not a smaller source.
    Slate::IlluminantSpecification Sizeless = Declaring;
    Sizeless.EmissionRadius                 = 0.0;

    Slate::OwnerIdentity SecondOwner;
    SecondOwner.SlotIndex    = 2u;
    SecondOwner.SlotGeneration = 1u;

    Report("A shape with no size is rejected",
           !Illuminants.Declare(SecondOwner, Sizeless).Resolved,
           "[-] a zero extent produces a single aliased pixel or nothing");

    Slate::IlluminantSpecification Spaceless = Declaring;
    Spaceless.DeclaredColour.SpaceIdentity   = 0u;

    Report("A colour with no space is rejected",
           !Illuminants.Declare(SecondOwner, Spaceless).Resolved,
           "[-] `36` §1: no bare triple");

    // 🔴 Exactly one atmospheric source. Two suns is a scene where the shadows fall one way and the sky the other.
    Slate::IlluminantSpecification Sun = Declaring;
    Sun.Emission                       = Slate::EmissionShape::Directional;
    Sun.AngularSize                    = 0.53;
    Sun.AtmosphericSource              = true;

    Report("A sun is registered",
           Illuminants.Declare(SecondOwner, Sun).Resolved
        && Illuminants.AtmosphericSource().Resolved,
           "[-] `28` reads exactly this one");

    Slate::OwnerIdentity ThirdOwner;
    ThirdOwner.SlotIndex    = 3u;
    ThirdOwner.SlotGeneration = 1u;

    Report("A second sun is rejected",
           !Illuminants.Declare(ThirdOwner, Sun).Resolved,
           "[-] one atmospheric source, by declaration");

    Report("The same sun may be amended",
           Illuminants.Amend(SecondOwner, Sun).Resolved,
           "[-] the exclusion excludes others, never itself");

    // 📝 A declared temperature is retained as authored and projected on demand — `36` §5.
    Slate::IlluminantSpecification Warm = Declaring;
    Warm.TemperatureDeclared             = true;
    Warm.Temperature                     = 5600.0;

    Discard(Illuminants.Declare(ThirdOwner, Warm));

    Report("A temperature is retained as authored",
           Illuminants.Resolve(ThirdOwner).Resolve().Temperature == 5600.0,
           "[K] the artist who set 5600 sees 5600");

    Report("A temperature projects to a colour",
           Illuminants.ResolveColour(ThirdOwner, Slate::DeclaredWorkingSpace()).Resolved,
           "[-] projected on demand, never written back");

    Report("Registration is ordered by identity",
           Illuminants.RegisteredCount() == 3u
        && Illuminants.Registered()[0].SlotIndex == 1u
        && Illuminants.Registered()[2].SlotIndex == 3u,
           "[-] stable across ticks, runs and machines");

    // 📐 Incidence at a position within the extent attenuates; beyond it, exactly zero.
    Slate::DocumentPosition Shaded;
    Shaded.PositionX = 100.0;

    const Slate::Deliver<Slate::IncidenceProjection> Near = Slate::ProjectIncidence(Declaring, Shaded);

    Report("Incidence resolves within the extent",
           Near.Resolved && Near.Resolve().Attenuation > 0.0 && Near.Resolve().SolidExtent > 0.0,
           "[sr] and the solid extent is never zero");

    Slate::DocumentPosition Distant;
    Distant.PositionX = 5000.0;

    const Slate::Deliver<Slate::IncidenceProjection> Far = Slate::ProjectIncidence(Declaring, Distant);

    Report("Beyond the declared extent attenuates to zero",
           Far.Resolved && Far.Resolve().Attenuation == 0.0,
           "[-] a declared cutoff, not a discovered threshold");

    // 🔴 The row that pays for the declared cutoff: brightening an illuminant re-derives nothing.
    const std::uint64_t BeforeIntensity = Illuminants.Revision();

    Slate::IlluminantSpecification Brighter = Declaring;
    Brighter.RadiantIntensity               = 40.0;
    Discard(Illuminants.Amend(FirstOwner, Brighter));

    Slate::IlluminantIndex Reach;

    std::vector<Slate::PartitionExtent> Extents(2);
    Extents[0].Minimum.PositionX    = -10.0;
    Extents[0].Maximum.PositionX =  10.0;
    Extents[0].Maximum.PositionY =  10.0;
    Extents[0].Maximum.PositionZ =  10.0;

    Extents[1].Minimum.PositionX    = 9000.0;
    Extents[1].Maximum.PositionX = 9010.0;
    Extents[1].Maximum.PositionY =   10.0;
    Extents[1].Maximum.PositionZ =   10.0;

    Report("The reach index derives",
           Reach.Derive(Illuminants, Extents).Resolved && Reach.SpannedCount() == 2u,
           "[-] one reaching set per partition");

    Report("A near partition is reached by the point illuminant",
           Reach.ReachingCount(0u) == 3u,
           "[-] point, sun and warm all reach it");

    // 📐 The distant partition is beyond both positioned illuminants' extents, and a directional shape reaches
    //    everything because it declares no position for an extent to be measured from.
    Report("A distant partition is reached by the directional alone",
           Reach.ReachingCount(1u) == 1u
        && Reach.Reaching(1u, 0u).Resolve() == SecondOwner,
           "[-] the declared extent bounds the positioned shapes");

    Report("The reaching set is in identity order",
           Reach.Reaching(0u, 0u).Resolve().SlotIndex == 1u
        && Reach.Reaching(0u, 2u).Resolve().SlotIndex == 3u,
           "[-] `02` §5's ordered recombination one layer up");

    Report("A radiant intensity change advanced the revision only",
           Illuminants.Revision() > BeforeIntensity && Reach.ReachingCount(0u) == 3u,
           "[-] the extent is declared, so nothing re-derived");

    Report("Nothing was truncated", Reach.TruncatedTotal() == 0u, "[-] three is below the capacity");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SPATIAL SUBDIVISION
//------------------------------------------------------------------------------------------------------------------------

void VerifySubdivision()
{
    std::printf("SpatialSubdivision\n");

    // 📐 One square in the plane at the origin, facing the third axis. Small enough to reason about and enough to
    //    exercise the fan triangulation, the exact edge classification and the object-space transform.
    Slate::TopologyStructure Imported;

    std::vector<Slate::DocumentPosition> Positions(4);
    Positions[0].PositionX = -1.0;  Positions[0].PositionY = -1.0;
    Positions[1].PositionX =  1.0;  Positions[1].PositionY = -1.0;
    Positions[2].PositionX =  1.0;  Positions[2].PositionY =  1.0;
    Positions[3].PositionX = -1.0;  Positions[3].PositionY =  1.0;

    Discard(Imported.DeclarePositions(Positions));
    Discard(Imported.DeclareFace({ 0u, 1u, 2u, 3u }));
    Discard(Imported.Seal());

    Slate::TopologyConditioning Conditioned;
    Discard(Conditioned.Condition(Imported));

    Slate::BoundingStructure Inner;

    Report("The inner structure builds",
           Inner.ConstructSubdivision(Imported, Conditioned).Resolved && Inner.FaceCount() == 1u,
           "[-] over the conditioned face extents");

    // 🔴 The revision gate: a conditioning describing a different seal indexes faces that have moved. A second
    //    topology, sealed separately — not the same one resealed, which is idempotent and issues nothing.
    Slate::TopologyStructure Reimported;
    Discard(Reimported.DeclarePositions(Positions));
    Discard(Reimported.DeclareFace({ 0u, 1u, 2u }));
    Discard(Reimported.Seal());

    Slate::BoundingStructure Mismatched;

    Report("A conditioning of another revision is rejected",
           !Mismatched.ConstructSubdivision(Reimported, Conditioned).Resolved,
           "[-] refuses rather than indexing faces that moved");

    Slate::DocumentPosition RayOrigin;
    RayOrigin.PositionZ = 5.0;

    const Slate::FaceIntersection Met = Inner.IntersectRay(RayOrigin, 0.0, 0.0, -1.0, 1000.0);

    Report("A ray meets the face",
           Met.Resolved && std::fabs(Met.Distance - 5.0) < 1.0e-9,
           "[mm] at the distance the geometry says");

    Report("The barycentric weights sum to one",
           Met.Resolved && std::fabs(Met.Weights[0] + Met.Weights[1] + Met.Weights[2] - 1.0) < 1.0e-9,
           "[-] one traversal serves every consumer");

    Report("A ray past the face misses",
           !Inner.IntersectRay(RayOrigin, 1.0, 0.0, 0.0, 1000.0).Resolved,
           "[-] and reports the miss rather than the nearest thing");

    Report("A ray beyond the furthest distance misses",
           !Inner.IntersectRay(RayOrigin, 0.0, 0.0, -1.0, 1.0).Resolved,
           "[mm] the bound is honoured");

    // 📐 The corner is exactly on two edges, so the exact predicate resolves zero on both and the hit is
    //    accepted. A filtered test misses along exactly the shared edges dense topology is made of.
    Slate::DocumentPosition CornerRay;
    CornerRay.PositionX = 1.0;  CornerRay.PositionY = -1.0;  CornerRay.PositionZ = 5.0;

    Report("A ray through a corner is a hit",
           Inner.IntersectRay(CornerRay, 0.0, 0.0, -1.0, 1000.0).Resolved,
           "[-] `02` §4's exact classification, on the edge");

    Slate::OctantSpace Outer;
    Slate::RegistrationIndex Subsets;

    Slate::AcceptedOwner Accepting;
    Accepting.Owner.SlotIndex    = 5u;
    Accepting.Owner.SlotGeneration = 1u;
    Accepting.Inner                   = &Inner;
    Accepting.Extent                  = Inner.Extent();

    Report("An owner is accepted",
           Outer.Accept(Accepting).Resolved && Outer.ConstructionOwed(),
           "[-] and the subdivision is owed a build");

    Report("An undeclared identity is rejected",
           !Outer.Accept(Slate::AcceptedOwner{}).Resolved,
           "[-] an undeclared identity occupies nothing");

    Discard(Outer.ConstructOctants());

    Report("Construction discharges the debt", !Outer.ConstructionOwed(), "[-] the shape is current");

    const Slate::ResolvedIntersection Resolved =
        Outer.IntersectRay(RayOrigin, 0.0, 0.0, -1.0, Subsets);

    Report("The outer traversal resolves an owner",
           Resolved.Resolved && Resolved.Owner == Accepting.Owner,
           "[-] the whole tuple, one traversal");

    Report("The resolved position is in document space",
           Resolved.Resolved && std::fabs(Resolved.Position.PositionZ) < 1.0e-9,
           "[mm] `78`'s manipulator plane reads this");

    // 🔴 `40` §3: exclusion is tested before descent, so a locked owner is never traversed at all.
    Discard(Subsets.Register(Accepting.Owner, Slate::SubsetSubject::Lock));

    Report("A locked owner is not traversed",
           !Outer.IntersectRay(RayOrigin, 0.0, 0.0, -1.0, Subsets).Resolved,
           "[-] excluded before descent, never after");

    Discard(Subsets.Unenrol(Accepting.Owner, Slate::SubsetSubject::Lock));
    Discard(Subsets.Register(Accepting.Owner, Slate::SubsetSubject::VisibilityExclusion));

    Report("A hidden owner is not traversed",
           !Outer.IntersectRay(RayOrigin, 0.0, 0.0, -1.0, Subsets).Resolved,
           "[-] the same test, the same cost");

    Discard(Subsets.Unenrol(Accepting.Owner, Slate::SubsetSubject::VisibilityExclusion));

    // 🔴 The property the two levels exist for: an owner move is a refit, and the inner structure is untouched.
    Slate::DecomposedTransform Moved;
    Moved.Translation.PositionX = 50.0;

    Slate::ConditionedExtent MovedExtent = Inner.Extent();
    MovedExtent.Minimum.PositionX    += 50.0;
    MovedExtent.Maximum.PositionX += 50.0;

    const std::uint32_t RecordsBefore = Outer.RecordCount();

    Report("An owner move is a refit",
           Outer.Refit(Accepting.Owner, Moved, MovedExtent).Resolved
        && Outer.RecordCount() == RecordsBefore
        && !Outer.ConstructionOwed(),
           "[-] the shape is untouched — `40` §4");

    Slate::DocumentPosition MovedRay;
    MovedRay.PositionX = 50.0;  MovedRay.PositionZ = 5.0;

    Report("The moved owner resolves at its new position",
           Outer.IntersectRay(MovedRay, 0.0, 0.0, -1.0, Subsets).Resolved,
           "[mm] object space is invariant under the motion");

    Report("A refitted owner no longer resolves at the old one",
           !Outer.IntersectRay(RayOrigin, 0.0, 0.0, -1.0, Subsets).Resolved,
           "[-] the refit was applied, not merely recorded");

    // 🔴 One traversal over the extent, never one per position inside it — `74` §4.
    Slate::ConditionedExtent Marquee;
    Marquee.Minimum.PositionX    = 40.0;
    Marquee.Minimum.PositionY    = -10.0;
    Marquee.Minimum.PositionZ    = -10.0;
    Marquee.Maximum.PositionX = 60.0;
    Marquee.Maximum.PositionY =  10.0;
    Marquee.Maximum.PositionZ =  10.0;

    Report("A marquee registers by containment",
           Outer.IntersectExtent(Marquee, true, Subsets).size() == 1u,
           "[-] one traversal, one registration");

    Slate::ConditionedExtent Grazing = Marquee;
    Grazing.Minimum.PositionX    = 50.5;
    Grazing.Maximum.PositionX = 60.0;

    Report("Containment and intersection differ",
           Outer.IntersectExtent(Grazing, true, Subsets).empty()
        && Outer.IntersectExtent(Grazing, false, Subsets).size() == 1u,
           "[-] wholly inside is not the same as overlapping");

    Report("A locked owner is absent from a marquee",
           [&]
           {
               Discard(Subsets.Register(Accepting.Owner, Slate::SubsetSubject::Lock));
               const bool Empty = Outer.IntersectExtent(Marquee, false, Subsets).empty();
               Discard(Subsets.Unenrol(Accepting.Owner, Slate::SubsetSubject::Lock));
               return Empty;
           }(),
           "[-] the same exclusion, the same place");

    Report("Withdrawal owes a rebuild",
           Outer.Withdraw(Accepting.Owner).Resolved && Outer.ConstructionOwed(),
           "[-] the shape no longer describes the population");

    // 📝 The domain subdivision answers a different question in a different space.
    Slate::AxisSpace Domain;

    std::vector<Slate::DomainExtent> Placements(2);
    Placements[0].MinimumX      = 0.0;   Placements[0].MinimumY     = 0.0;
    Placements[0].MaximumX   = 0.6;   Placements[0].MaximumY  = 0.6;
    Placements[0].PlacementIndex = 10u;  Placements[0].SequenceIndex = 1u;

    Placements[1].MinimumX      = 0.4;   Placements[1].MinimumY     = 0.4;
    Placements[1].MaximumX   = 1.0;   Placements[1].MaximumY  = 1.0;
    Placements[1].PlacementIndex = 11u;  Placements[1].SequenceIndex = 2u;

    Domain.ConstructAxes(Placements);

    Report("A domain position resolves a placement",
           Domain.Resolve(0.2, 0.2).Resolved && Domain.Resolve(0.2, 0.2).Resolve() == 10u,
           "[-] `74` precedence 1");

    // 🔴 The topmost containing placement wins, by `56` sequence order — never the first declared.
    Report("Overlapping placements resolve topmost",
           Domain.Resolve(0.5, 0.5).Resolve() == 11u,
           "[-] never by declaration order");

    Report("A position outside every placement refuses",
           !Domain.Resolve(1.5, 1.5).Resolved,
           "[-] refuses rather than resolving the nearest");

    Report("A placement refits without a rebuild",
           Domain.Refit(10u, Placements[1]).Resolved,
           "[-] `40` §4's placement row");

    Report("An unknown placement ordinal refuses",
           !Domain.Refit(99u, Placements[0]).Resolved,
           "[-] no placement carries it");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CHART PARTITION
//------------------------------------------------------------------------------------------------------------------------

// 📐 A three-by-three grid of quads in the plane. Genuinely a disc, so it flattens in one chart with no derived
//    seam; cutting it across the middle must produce two. Both are checked, because a partitioner that always
//    cuts and one that never cuts each pass half of this on their own.
void VerifyPartition()
{
    std::printf("ChartPartition\n");

    Slate::TopologyStructure Imported;

    std::vector<Slate::DocumentPosition> Positions(16);

    for (std::uint32_t Y = 0u; Y < 4u; ++Y)
    {
        for (std::uint32_t X = 0u; X < 4u; ++X)
        {
            Positions[Y * 4u + X].PositionX = static_cast<double>(X);
            Positions[Y * 4u + X].PositionY = static_cast<double>(Y);
        }
    }

    Discard(Imported.DeclarePositions(Positions));

    for (std::uint32_t Y = 0u; Y < 3u; ++Y)
    {
        for (std::uint32_t X = 0u; X < 3u; ++X)
        {
            const std::uint32_t Corner = Y * 4u + X;

            Discard(Imported.DeclareFace({ Corner, Corner + 1u, Corner + 5u, Corner + 4u }));
        }
    }

    Discard(Imported.Seal());

    Slate::TopologyConditioning Conditioned;
    Discard(Conditioned.Condition(Imported));

    Slate::SeamSpecification Seams;

    Report("A seam of one vertex is rejected",
           !Seams.DeclareAuthored(3u, 3u).Resolved,
           "[-] one vertex is not an edge");

    Slate::PartitionSpecification Declaring;
    Slate::WorkCancellation       Posed;
    Slate::WorkProgress           Progressed;

    const Slate::Deliver<Slate::DerivedPartition> Derived =
        Slate::Derive(Imported, Conditioned, Seams, Declaring, Posed, Progressed);

    Report("A disc derives one chart",
           Derived.Resolved && Derived.Resolve().Metrics.ChartCount == 1u,
           "[-] connected across every non-seam edge");

    Report("A disc needs no derived seam",
           Derived.Resolved && Derived.Resolve().Metrics.DerivedSeamCount == 0u,
           "[-] the authored set sufficed");

    Report("No fold survives",
           Derived.Resolved && Derived.Resolve().Metrics.FoldCount == 0u,
           "[-] `68` §4.1: a fold is a failure, never a distortion value");

    // 🔴 Every corner lands strictly inside the unit domain, gap included. A coordinate on the boundary means the
    //    apron of an edge tile reads outside the domain, which is the fringe `68` §5's gap exists to prevent.
    bool WithinDomain = Derived.Resolved;

    for (const Slate::DomainCoordinate& Held : Derived.Resolve().CornerCoordinates)
    {
        if (Held.CoordinateX <= 0.0f || Held.CoordinateX >= 1.0f
         || Held.CoordinateY <= 0.0f || Held.CoordinateY >= 1.0f)
        {
            WithinDomain = false;
        }
    }

    Report("Every coordinate lands inside the domain", WithinDomain, "[-] strictly, gap included");

    Report("Occupancy is reported and is a fraction",
           Derived.Resolved
        && Derived.Resolve().Metrics.Occupancy > 0.0
        && Derived.Resolve().Metrics.Occupancy <= 1.0,
           "[-] `86`'s `68` §5 measure");

    // 📐 A planar chart flattens with bounded distortion but not with none — the boundary is mapped to a circle,
    //    so a square necessarily stretches. What is checked is that the measure was taken at all.
    Report("Distortion is measured separately",
           Derived.Resolved
        && Derived.Resolve().Charts[0].Distortion.MeasureDeclared
        && Derived.Resolve().Metrics.MaximumAreaRatio >= 1.0,
           "[-] area and angle, never one number for both");

    // 🔴 The seam is what cuts, and it is over welded positions rather than imported vertices.
    Discard(Seams.DeclareAuthored(1u, 5u));
    Discard(Seams.DeclareAuthored(5u, 9u));
    Discard(Seams.DeclareAuthored(9u, 13u));

    const Slate::Deliver<Slate::DerivedPartition> Cut =
        Slate::Derive(Imported, Conditioned, Seams, Declaring, Posed, Progressed);

    Report("An authored seam cuts the topology",
           Cut.Resolved && Cut.Resolve().Metrics.ChartCount == 2u,
           "[-] two charts, from one cut");

    Report("Chart identity is the least face ordinal held",
           Cut.Resolved
        && (Cut.Resolve().Charts[0].IdentityIndex == 0u || Cut.Resolve().Charts[1].IdentityIndex == 0u),
           "[-] stable where the chart is unchanged — `24` §3 keys on it");

    Slate::ChartPartition Current;

    Report("Nothing stands before adoption",
           !Current.PartitionCurrent() && !Current.Coordinate(0u).Resolved,
           "[-] the previous partition stands until the solve completes — `68` §7");

    Report("Adoption advances the revision",
           Current.Adopt(Cut.Resolve()).Resolved && Current.Revision() == 1u,
           "[-] what `24` §3 keys on and `20` promotes against");

    const std::uint64_t BeforeSecond = Current.Revision();
    Discard(Current.Adopt(Derived.Resolve()));

    Report("A re-partition advances it again",
           Current.Revision() > BeforeSecond,
           "[-] every artefact in the old domain is discoverably stale");

    Report("An empty partition is rejected",
           !Current.Adopt(Slate::DerivedPartition{}).Resolved,
           "[-] a partition carrying no chart");

    // 🔴 `68` §2: the authored set survives a re-partition. This is the hour of the artist's work the whole
    //    two-set split exists to protect.
    Seams.DeclareDerived(2u, 6u);
    Seams.ReclaimDerived();

    Report("Reclaiming derived seams leaves the authored set",
           Seams.AuthoredCount() == 3u && Seams.DerivedCount() == 0u,
           "[-] an automatic partition never erases marked seams");

    Report("Only an authored amendment advances the seam revision",
           [&]
           {
               const std::uint64_t Before = Seams.Revision();
               Seams.DeclareDerived(2u, 6u);
               const bool Unmoved = Seams.Revision() == Before;
               Seams.ReclaimDerived();
               return Unmoved;
           }(),
           "[-] a derived seam is a consequence, never an input");

    Slate::DomainSpace Arranged;

    std::vector<Slate::ChartExtent> Extents(3);
    Extents[0].Width = 0.4;  Extents[0].Height = 0.3;  Extents[0].ChartIndex = 0u;
    Extents[1].Width = 0.3;  Extents[1].Height = 0.5;  Extents[1].ChartIndex = 1u;
    Extents[2].Width = 0.2;  Extents[2].Height = 0.2;  Extents[2].ChartIndex = 2u;

    Report("A per-chart scale is rejected",
           !Arranged.Arrange(Extents, false).Resolved,
           "[-] `68` §10's open row is not decided by accident");

    Report("Charts arrange at a common scale",
           Arranged.Arrange(Extents, true).Resolved && Arranged.SettledScale() > 0.0,
           "[-] one texel covers the same area on every chart");

    // 🔴 `68` §5: at least one apron between adjacent charts, and at least one from the domain edge. Measured
    //    rather than asserted, because the packing is where the gap is actually applied.
    bool GapHeld = true;

    const double Gap = Slate::DeclaredGap();

    for (std::size_t Earlier = 0u; Earlier < Arranged.Placements().size(); ++Earlier)
    {
        const Slate::ChartPlacement& Alpha = Arranged.Placements()[Earlier];

        if (Alpha.MinimumX < Gap || Alpha.MinimumY < Gap)
            GapHeld = false;

        const double AlphaX  = Alpha.MinimumX  + Extents[Earlier].Width  * Alpha.Scale;
        const double AlphaY = Alpha.MinimumY + Extents[Earlier].Height * Alpha.Scale;

        if (AlphaX > 1.0 || AlphaY > 1.0)
            GapHeld = false;

        for (std::size_t Later = Earlier + 1u; Later < Arranged.Placements().size(); ++Later)
        {
            const Slate::ChartPlacement& Beta = Arranged.Placements()[Later];

            const double BetaX  = Beta.MinimumX  + Extents[Later].Width  * Beta.Scale;
            const double BetaY = Beta.MinimumY + Extents[Later].Height * Beta.Scale;

            const bool ApartX  = AlphaX + Gap <= Beta.MinimumX  || BetaX  + Gap <= Alpha.MinimumX;
            const bool ApartY = AlphaY + Gap <= Beta.MinimumY || BetaY + Gap <= Alpha.MinimumY;

            if (!ApartX && !ApartY)
                GapHeld = false;
        }
    }

    Report("Adjacent charts are separated by at least the apron",
           GapHeld,
           "[-] narrower, and every chart edge carries a neighbour's fringe");

    Report("The arrangement is reproducible",
           [&]
           {
               Slate::DomainSpace Repeated;
               Discard(Repeated.Arrange(Extents, true));
               return Repeated.SettledScale() == Arranged.SettledScale();
           }(),
           "[-] bit for bit; the shelf order is scale-invariant");

    // 📐 The solver's own guarantee, exercised where the partition cannot reach it: a ceiling of one cannot
    //    converge, and reporting it as convergence is the ambiguity `02` §5 exists to close.
    // 🔴 The whole grid, not one quad. A chart whose every position lies on the boundary loop has no interior to
    //    relax, so its residual is zero on the first sweep and it converges whatever the ceiling — which would
    //    pass the ceiling check for the one reason that makes the check prove nothing.
    Slate::UnwrapSpecification Solving;
    Solving.Positions    = Positions;
    Solving.ContourLoop = { 0u, 1u, 2u, 3u, 7u, 11u, 15u, 14u, 13u, 12u, 8u, 4u };

    for (std::uint32_t Y = 0u; Y < 3u; ++Y)
    {
        for (std::uint32_t X = 0u; X < 3u; ++X)
        {
            const std::uint32_t Corner = Y * 4u + X;

            Solving.TriangleCorners.insert(Solving.TriangleCorners.end(),
                                           { Corner, Corner + 1u, Corner + 5u,
                                             Corner, Corner + 5u, Corner + 4u });
        }
    }

    Report("A converged solve says so",
           Slate::Solve(Solving).Resolved
        && Slate::Solve(Solving).Resolve().Cause == Slate::TerminationCause::CriterionSatisfied,
           "[-] `02` §5's Convergent guarantee");

    Slate::UnwrapSpecification Limited = Solving;
    Limited.IterationLimit           = 1u;
    Limited.ConvergenceCriterion       = 1.0e-18;

    Report("A ceiling termination says so too",
           Slate::Solve(Limited).Resolve().Cause == Slate::TerminationCause::LimitReached,
           "[-] the last iterate, never presented as convergence");

    Slate::UnwrapSpecification Loopless = Solving;
    Loopless.ContourLoop                = { 0u, 1u };

    Report("A boundary that is not a loop is rejected",
           !Slate::Solve(Loopless).Resolved,
           "[-] no disc, no boundary-first parameterisation");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTAKE
//------------------------------------------------------------------------------------------------------------------------

void VerifyIntake()
{
    std::printf("AssetInterchange\n");

    Slate::AssetInterchange   Interchange;
    Slate::IntakeIndex        Recorded;
    Slate::TopologyStructure  Into;

    Slate::DecodedTopology Decoded;

    Report("A source with no position is rejected",
           !Interchange.IntakeTopology(Decoded, Into, Recorded).Resolved,
           "[-] `50` §3 gives it no default");

    Decoded.Positions.resize(4);
    Decoded.Positions[1].PositionX = 1.0;
    Decoded.Positions[2].PositionX = 1.0;
    Decoded.Positions[2].PositionY = 1.0;
    Decoded.Positions[3].PositionY = 1.0;
    Decoded.OriginPath             = "figure.topology";

    Report("A source with no face indexing is rejected",
           !Interchange.IntakeTopology(Decoded, Into, Recorded).Resolved,
           "[-] rejected, never defaulted");

    Decoded.Faces.push_back({ 0u, 1u, 9u });

    Report("A partially invalid intake registers nothing",
           !Interchange.IntakeTopology(Decoded, Into, Recorded).Resolved
        && Into.VertexCount() == 0u,
           "[-] `50` §8: half a topology is an owner the artist will export");

    Decoded.Faces[0] = { 0u, 1u, 2u, 3u };

    Report("A faithful intake registers",
           Interchange.IntakeTopology(Decoded, Into, Recorded).Resolved
        && Into.Sealed()
        && Into.FaceCount() == 1u,
           "[-] sealed and immutable for the run");

    // 🔴 `50` §3: the two assuming rows are the two that produce a plausible, wrong result. Both are recorded.
    Report("A silent unit convention is recorded as an assumption",
           Recorded.AssumptionCount() == 1u
        && Recorded.Resolve("figure.topology").Resolve().Assumed == Slate::AssumedSubject::UnitScale,
           "[-] a model at a hundredth of its size still renders");

    Slate::DecodedTopology Scaled = Decoded;
    Scaled.OriginPath             = "scaled.topology";
    Scaled.UnitScale              = 10.0;
    Scaled.UnitScaleDeclared      = true;

    Slate::TopologyStructure ScaledInto;
    Discard(Interchange.IntakeTopology(Scaled, ScaledInto, Recorded));

    Report("Unit scale is applied once, at intake",
           ScaledInto.Positions()[1].PositionX == 10.0,
           "[mm] never carried as a per-owner multiplier");

    Report("A declared convention assumes nothing",
           Recorded.AssumptionCount() == 1u,
           "[-] the record names only what was guessed");

    Report("An absent enrollment defaults to one material",
           ScaledInto.MaterialRegistration().size() == ScaledInto.FaceCount(),
           "[-] `50` §3's last-resort row");

    Slate::DecodedImage Image;

    Report("An image of no extent is rejected",
           !Interchange.IntakeImage(Image, Recorded).Resolved,
           "[-] there is nothing to decode");

    Image.Width          = 64u;
    Image.Height         = 64u;
    Image.ComponentCount = 3u;
    Image.BitDepth       = 16u;
    Image.OriginPath     = "albedo.image";
    Image.Original.assign(64u * 64u * 3u * 2u, 0u);

    Report("A silent colour space is recorded as an assumption",
           Interchange.IntakeImage(Image, Recorded).Resolved
        && Recorded.AssumptionCount() == 2u,
           "[-] an image decoded as linear still looks like an image");

    Report("The original is retained for re-conversion",
           !Image.Original.empty() && Image.BitDepth == 16u,
           "[-] never narrowed at intake — `36` §3 re-converts from it");

    Slate::ReportSequence Reporting;
    const Slate::TickSequence HostTimeline;

    Recorded.Report(Reporting, HostTimeline.Advance());

    Report("Every assumption reaches the register",
           Reporting.RetainedCount() == 2u,
           "[-] recorded and reported, never silent");

    Recorded.Report(Reporting, HostTimeline.Advance());

    Report("An assumption is reported once",
           Reporting.AppendedCount() == 2u,
           "[-] a second tick appends nothing further");

    Slate::MaterialIndex Materials;

    const std::uint32_t MaterialIndex = Materials.Declare("Textured metal").Resolve();

    Slate::ChannelSpecification Albedo;
    Albedo.Source                       = Slate::ChannelSource::Layered;
    Albedo.Measured                     = Slate::ChannelMeasure::Reflectance;
    Albedo.ConstantColour.SpaceIdentity = Slate::WorkingSpaceIdentity;
    Albedo.DefaultColour.SpaceIdentity  = Slate::WorkingSpaceIdentity;

    Discard(Materials.Amend(MaterialIndex).Resolve()->DeclareChannel(Slate::ChannelSubject::AlbedoColour, Albedo));

    Slate::EmissionSpecification Emitting;

    Report("An emission producing no image is rejected",
           !Interchange.DeclareEmission(Emitting, Materials).Resolved,
           "[-] nothing would leave");

    Slate::EmittedImage Packed;
    Packed.ExtentTexels                                                             = 2048u;
    Packed.Occupying[static_cast<std::size_t>(Slate::ComponentSlot::Red)]            = Slate::ChannelSubject::AmbientOcclusion;
    Packed.ComponentOccupied[static_cast<std::size_t>(Slate::ComponentSlot::Red)]    = true;
    Packed.Occupying[static_cast<std::size_t>(Slate::ComponentSlot::Green)]          = Slate::ChannelSubject::Roughness;
    Packed.ComponentOccupied[static_cast<std::size_t>(Slate::ComponentSlot::Green)]  = true;
    Packed.Occupying[static_cast<std::size_t>(Slate::ComponentSlot::Blue)]           = Slate::ChannelSubject::Metallic;
    Packed.ComponentOccupied[static_cast<std::size_t>(Slate::ComponentSlot::Blue)]   = true;

    Emitting.Images.push_back(Packed);
    Emitting.NamePattern = "{Owner}_{Material}_{Channel}_{Extent}";

    Report("A declared arrangement is accepted",
           Interchange.DeclareEmission(Emitting, Materials).Resolved,
           "[-] `50` §5.1: declared and presented, never conventional");

    Slate::EmittedImage Colour;
    Colour.ExtentTexels                                                           = 4096u;
    Colour.Occupying[static_cast<std::size_t>(Slate::ComponentSlot::Red)]          = Slate::ChannelSubject::AlbedoColour;
    Colour.ComponentOccupied[static_cast<std::size_t>(Slate::ComponentSlot::Red)]  = true;

    Slate::EmissionSpecification Spaceless = Emitting;
    Spaceless.Images.push_back(Colour);

    Report("A colour channel with no declared space is rejected",
           !Interchange.DeclareEmission(Spaceless, Materials).Resolved,
           "[-] the consumer would decode it by guessing");

    Colour.SpaceIdentity = Slate::WorkingSpaceIdentity;

    Slate::EmissionSpecification Spaced = Emitting;
    Spaced.Images.push_back(Colour);

    Report("Per-image extents differ",
           Interchange.DeclareEmission(Spaced, Materials).Resolved,
           "[-] not one extent for every channel");

    Slate::EmissionSpecification Doubled = Spaced;
    Doubled.Images.push_back(Packed);

    Report("A channel emitted twice is rejected",
           !Interchange.DeclareEmission(Doubled, Materials).Resolved,
           "[-] the consumer reads whichever it loaded second");

    Report("The naming pattern resolves",
           Slate::ResolveName(Emitting.NamePattern, "Figure", "Metal", "Roughness", 2048u)
        == "Figure_Metal_Roughness_2048",
           "[-] over owner, material, channel and extent");

    Report("An unrecognised substitution is left verbatim",
           Slate::ResolveName("{Owner}_{Absent}", "Figure", "Metal", "Roughness", 2048u)
        == "Figure_{Absent}",
           "[-] emptied, and every name collides with every other");

    Slate::DecodedTopology Refusing = Decoded;
    Refusing.OriginPath             = "animated.topology";
    Refusing.UnsupportedNamed.push_back("animation track");

    Slate::TopologyStructure RefusingInto;
    Discard(Interchange.IntakeTopology(Refusing, RefusingInto, Recorded));

    Report("An unsupported construct is named at intake",
           Interchange.Unsupported().size() == 1u,
           "[-] `50` §6: not at export, by which time it is missed");

    Interchange.Report(Reporting, HostTimeline.Advance());

    Report("It reaches the register once",
           [&]
           {
               const std::uint64_t Appended = Reporting.AppendedCount();
               Interchange.Report(Reporting, HostTimeline.Advance());
               return Reporting.AppendedCount() == Appended;
           }(),
            "[-] a second call appends nothing further");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE STROKE
//------------------------------------------------------------------------------------------------------------------------

// 📐 The stroke is verified at the coarsest reduction level, whose cells `20` §3 keeps permanently resident. That
//    makes the whole path exercisable without a promotion budget, and the deferral check below then textures at the
//    finest level — where nothing is resident — so both halves of `22` §2's rule are measured rather than assumed.
void VerifyStroke()
{
    std::printf("ImpressionSequence\n");

    Report("A working extent that is no level is rejected",
           !Slate::TexturingLevelOf(100u).Resolved,
           "[-] refuses rather than rounding to the nearest");

    const Slate::Deliver<std::uint32_t> Coarsest = Slate::TexturingLevelOf(Slate::CoverageTileTexels);

    Report("The coarsest extent resolves its level",
           Coarsest.Resolved && Coarsest.Resolve() == Slate::ReductionLevelCount - 1u,
           "[-] one cell of one hundred and twenty-eight texels");

    Slate::SurfaceTileSpace Residency;

    Report("The residency constructs",
           Residency.ConstructSurfaceTiles(0u, 16u, 64u).Resolved,
           "[-] the permanent levels are resident");

    // 📝 One textured entry at the coarsest extent, three components — a colour channel and nothing else. The
    //    placement is supplied rather than derived, per `00` §12's open packing row.
    Slate::SurfaceLayerSequence Content;

    Slate::LayerSpecification Texturing;
    Texturing.Source                 = Slate::LayerContentSource::TexturedImpressions;
    Texturing.Textured.ExtentTexels   = Slate::CoverageTileTexels;
    Texturing.Textured.ComponentCount = 3u;
    Texturing.Textured.Texels.assign(static_cast<std::size_t>(Slate::CoverageTileTexels)
                                 * Slate::CoverageTileTexels * 3u, 0.0f);

    const Slate::Deliver<Slate::LayerIdentity> Appended = Content.Append(Texturing);

    Report("A textured entry appends", Appended.Resolved, "[-] at the declared extent");

    if (!Appended.Resolved)
        return;

    Slate::LayerSpecification Analytic;
    Analytic.Source        = Slate::LayerContentSource::AnalyticResolution;
    Analytic.SourceIndex = 1u;

    const Slate::Deliver<Slate::LayerIdentity> Described = Content.Append(Analytic);

    Report("A described entry refuses amendment",
           Described.Resolved && !Content.AmendTextured(Described.Resolve()).Resolved,
           "[-] `56` §3: it stores a description, not texels");

    Slate::BrushSpecification Brush;

    Slate::ImpressionShape Shape;
    Shape.Source  = Slate::ShapeSource::Analytic;
    Shape.Profile = Slate::ProfileSubject::Linear;

    Discard(Brush.DeclareShape(Shape));
    Discard(Brush.DeclareExtent(0.1));
    Discard(Brush.DeclareSpacing(0.25));

    Slate::BrushChannelValue Albedo;
    Albedo.Channel                     = Slate::ChannelSubject::AlbedoColour;
    Albedo.ColourDeclared              = true;
    Albedo.ColourValue.RedCoordinate   = 1.0;
    Albedo.ColourValue.SpaceIdentity   = Slate::WorkingSpaceIdentity;

    Report("The brush declares a channel",
           Brush.DeclareChannel(Albedo).Resolved,
           "[-] a value per channel, never a bare colour");

    Slate::ChannelPlacement Placing;
    Placing.Channel          = Slate::ChannelSubject::AlbedoColour;
    Placing.ComponentIndex = 0u;
    Placing.ComponentSpan    = 3u;

    Slate::StrokeDeclaration Declaring;
    Declaring.Subject        = Appended.Resolve();
    Declaring.WorkingExtent  = Slate::CoverageTileTexels;
    Declaring.ComponentCount = 3u;
    Declaring.StrokeSeed     = 7u;
    Declaring.Placements.push_back(Placing);

    Slate::ImpressionSequence Stroke;

    Slate::StrokeDeclaration Mismatched = Declaring;
    Mismatched.Placements[0].ComponentSpan = 1u;

    Report("A placement of the wrong span is rejected",
           !Stroke.Open(Mismatched, Brush).Resolved,
           "[-] a colour occupies three components");

    Slate::StrokeDeclaration Overrunning = Declaring;
    Overrunning.Placements[0].ComponentIndex = 2u;

    Report("A placement past the components is rejected",
           !Stroke.Open(Overrunning, Brush).Resolved,
           "[-] it would write into the next texel");

    Report("The stroke opens", Stroke.Open(Declaring, Brush).Resolved, "[-] nothing is recorded yet");

    Report("A second open is rejected",
           !Stroke.Open(Declaring, Brush).Resolved,
           "[-] one stroke at a time");

    // 📐 A straight path of length 0.4 at a spacing of 0.25 × 0.1 places one impression at the origin and one
    //    every 0.025 thereafter — seventeen in all. Measured rather than asserted, because the spacing comes
    //    from the previously resolved brush and an off-by-one there is invisible in the textured result.
    const Slate::TickSequence StrokeTimeline;

    Slate::StrokeArrival Beginning;
    Beginning.SurfaceResolved       = true;
    Beginning.PositionX         = 0.3;
    Beginning.PositionY        = 0.5;
    Beginning.Incoming.Arrival      = StrokeTimeline.Advance();

    Discard(Stroke.Amend(Beginning));

    Report("The first arrival places an impression",
           Stroke.ImpressionCount() == 1u,
           "[-] a tap lays down colour; it does not wait for a tangent");

    Slate::StrokeArrival Ending = Beginning;
    Ending.PositionX        = 0.7;
    Ending.Incoming.Arrival     = StrokeTimeline.Advance();

    Discard(Stroke.Amend(Ending));

    Report("The path resamples at the brush's spacing",
           Stroke.ImpressionCount() == 17u,
           "[-] seventeen over four tenths, at one fortieth");

    Report("The path length is the domain distance",
           std::fabs(Stroke.PathLength() - 0.4) < 1.0e-9,
           "[-] resampled in the domain, never in pixels");

    // 🔴 A break must not interpolate. A stroke that leaves the surface and returns textures no line between the
    //    two places, and the artist cannot undo half of one stroke to remove one.
    const std::uint32_t BeforeBreak = Stroke.ImpressionCount();

    Slate::StrokeArrival Left;
    Left.SurfaceResolved   = false;
    Left.Incoming.Arrival  = StrokeTimeline.Advance();

    Discard(Stroke.Amend(Left));

    Slate::StrokeArrival Returned = Beginning;
    Returned.PositionX        = 0.1;
    Returned.PositionY       = 0.1;
    Returned.Incoming.Arrival     = StrokeTimeline.Advance();

    Discard(Stroke.Amend(Returned));

    Report("A broken path interpolates nothing",
           Stroke.ImpressionCount() == BeforeBreak,
           "[-] the gap is not textured across");

    Slate::RequestQueue Requesting;

    const Slate::Deliver<Slate::ResolvedRun> Ran = Stroke.Resolve(Residency, Requesting, 1u);

    Report("Every impression resolved",
           Ran.Resolved && Ran.Resolve().DeferredCount == 0u && Stroke.PendingCount() == 0u,
           "[-] the coarsest level is permanently resident");

    Report("The accumulation claimed one cell",
           Stroke.Accumulation().ReservedCount() == 1u,
           "[-] one cell at the coarsest level");

    Report("The tile it touched is uncommitted",
           Residency.Cells().UncommittedCount() == 1u,
           "[-] `20` §5: it is never evicted while the stroke is open");

    Slate::RevisionSequence Revised;

    const Slate::Deliver<Slate::SealedStroke> Sealed =
        Stroke.Seal(Content, Revised, Residency, 1000000000ull);

    Report("The stroke seals one transaction",
           Sealed.Resolved && Revised.Committed().size() == 1u,
           "[-] one stroke, one transaction — never one per sample");

    Report("The gate is withdrawn at Seal",
           Residency.Cells().UncommittedCount() == 0u,
           "[-] the texture is in `56`; the tile is a projection again");

    Report("The seed travelled with the transaction",
           Sealed.Resolved && Sealed.Resolve().Recorded.StrokeSeed == 7u,
           "[-] `58` §6: parameters recorded, never a reference");

    const Slate::Deliver<const Slate::LayerSpecification*> Written = Content.Resolve(Appended.Resolve());

    // 📐 The impression at the path's origin covers the texel under it at full coverage, so the red component
    //    there is the brush's own. A zero would mean the coverage never reached the entry at all.
    const std::size_t Centre = (static_cast<std::size_t>(64) * Slate::CoverageTileTexels + 38u) * 3u;

    Report("The accumulated coverage reached the entry",
           Written.Resolved && Written.Resolve()->Textured.Texels[Centre] > 0.99f,
           "[-] applied once, at the stroke's own combination");

    Report("A texel the stroke never reached is untouched",
           Written.Resolved && Written.Resolve()->Textured.Texels[0] == 0.0f,
           "[-] bounded by the impressions, not by the surface");

    Report("The inverse restores what stood before",
           Sealed.Resolved
        && Slate::Restore(Sealed.Resolve(), Content).Resolved
        && Content.Resolve(Appended.Resolve()).Resolve()->Textured.Texels[Centre] == 0.0f,
           "[-] extent-bounded, and replayed rather than snapshotted");

    // 🔴 `22` §2's other half: an impression whose cells are not resident at the texturing level demands and
    //    defers. Nothing is dropped and nothing resolves coarse, so the pending count survives the rotation.
    Slate::SurfaceLayerSequence FineContent;

    Slate::LayerSpecification Fine;
    Fine.Source                 = Slate::LayerContentSource::TexturedImpressions;
    Fine.Textured.ExtentTexels   = Slate::MaximumWorkingEdge;
    Fine.Textured.ComponentCount = 1u;

    Slate::StrokeDeclaration Deferring = Declaring;
    Deferring.WorkingExtent  = Slate::MaximumWorkingEdge;
    Deferring.ComponentCount = 3u;
    Deferring.Subject        = Appended.Resolve();

    Slate::ImpressionSequence Deferred;

    Discard(Deferred.Open(Deferring, Brush));
    Discard(Deferred.Amend(Beginning));

    const std::uint64_t DemandedBefore = Requesting.RecordedCount();

    const Slate::Deliver<Slate::ResolvedRun> Waiting = Deferred.Resolve(Residency, Requesting, 2u);

    Report("A non-resident cell defers rather than coarsening",
           Waiting.Resolved && Waiting.Resolve().DeferredCount == 1u && Deferred.PendingCount() == 1u,
           "[-] texture at the wrong resolution is permanently wrong");

    Report("The deferral recorded a demand",
           Requesting.RecordedCount() > DemandedBefore,
           "[-] demanded and deferred, never dropped");

    Report("The accumulation stayed empty",
           Deferred.Accumulation().ReservedCount() == 0u,
           "[-] nothing partial was written");

    Deferred.Abandon(Residency);

    Report("Abandonment records nothing",
           Revised.Committed().size() == 1u && !Deferred.StrokeOpen(),
           "[-] the prior contents stand");

    // 🔴 `22` §4.1: a speculative extent never commits and never pins a tile. Both are measured, because the
    //    second is the property that separates a preview from an uncommitted stroke.
    Slate::StrokeDeclaration Previewing = Declaring;
    Previewing.Speculative              = true;

    Slate::ImpressionSequence Preview;

    Discard(Preview.Open(Previewing, Brush));
    Discard(Preview.Amend(Beginning));
    Discard(Preview.Resolve(Residency, Requesting, 3u));

    Report("A speculative extent pins nothing",
           Preview.Accumulation().ReservedCount() != 0u && Residency.Cells().UncommittedCount() == 0u,
           "[-] hovering never exhausts residency");

    Report("A speculative extent never seals",
           !Preview.Seal(Content, Revised, Residency, 2000000000ull).Resolved,
           "[-] a preview is not a stroke the artist made");

    Report("A speculative extent reclaims per rotation",
           Preview.ReclaimSpeculative().Resolved && Preview.Accumulation().ReservedCount() == 0u,
           "[-] discarded and re-resolved, `22` §4.1");

    Preview.Abandon(Residency);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE POINTER
//------------------------------------------------------------------------------------------------------------------------

// 📐 One square in the plane at the origin, facing the third axis, with a domain coordinate per corner. Enough to
//    exercise the unprojection, the traversal, the barycentric domain interpolation and the marquee at once.
void VerifyPointer()
{
    std::printf("PointerIntersection\n");

    Slate::TopologyStructure Imported;

    std::vector<Slate::DocumentPosition> Positions(4);
    Positions[0].PositionX = -1.0;  Positions[0].PositionY = -1.0;
    Positions[1].PositionX =  1.0;  Positions[1].PositionY = -1.0;
    Positions[2].PositionX =  1.0;  Positions[2].PositionY =  1.0;
    Positions[3].PositionX = -1.0;  Positions[3].PositionY =  1.0;

    Discard(Imported.DeclarePositions(Positions));
    Discard(Imported.DeclareFace({ 0u, 1u, 2u, 3u }));

    std::vector<Slate::DomainCoordinate> Coordinates(4);
    Coordinates[0].CoordinateX = 0.0f;  Coordinates[0].CoordinateY = 0.0f;
    Coordinates[1].CoordinateX = 1.0f;  Coordinates[1].CoordinateY = 0.0f;
    Coordinates[2].CoordinateX = 1.0f;  Coordinates[2].CoordinateY = 1.0f;
    Coordinates[3].CoordinateX = 0.0f;  Coordinates[3].CoordinateY = 1.0f;

    Discard(Imported.DeclareCoordinates(Coordinates));
    Discard(Imported.Seal());

    Slate::TopologyConditioning Conditioned;
    Discard(Conditioned.Condition(Imported));

    Slate::BoundingStructure Inner;
    Discard(Inner.ConstructSubdivision(Imported, Conditioned));

    Slate::OwnerIdentity Subject;
    Subject.SlotIndex    = 5u;
    Subject.SlotGeneration = 1u;

    Slate::AcceptedOwner Accepting;
    Accepting.Owner = Subject;
    Accepting.Inner    = &Inner;
    Accepting.Extent   = Inner.Extent();

    Slate::OctantSpace Outer;
    Discard(Outer.Accept(Accepting));
    Discard(Outer.ConstructOctants());

    Report("The subdivision surrenders its record",
           Outer.Current(Subject).Resolved
        && Outer.Current(Subject).Resolve().Owner == Subject,
           "[-] the extent a marquee narrows against");

    Slate::CameraSpecification Declaring;
    Declaring.Placement.Translation.PositionZ = 10.0;
    Declaring.SensorProportion                = 1.0;

    Slate::CameraProjection Camera;

    Slate::OwnerIdentity CameraOwner;
    CameraOwner.SlotIndex    = 3u;
    CameraOwner.SlotGeneration = 1u;

    Discard(Camera.Declare(CameraOwner, Declaring));

    Report("An unreconciled camera refuses a ray",
           !Slate::ProjectPointerRay(Camera, 256.0, 256.0, 512u, 512u).Resolved,
           "[-] the standing projection is stale");

    Discard(Camera.Reconcile());

    const Slate::Deliver<Slate::ProjectedRay> Centred =
        Slate::ProjectPointerRay(Camera, 256.0, 256.0, 512u, 512u);

    Report("A centred pointer casts along the view direction",
           Centred.Resolved
        && std::fabs(Centred.Resolve().DirectionX) < 1.0e-12
        && std::fabs(Centred.Resolve().DirectionY) < 1.0e-12
        && std::fabs(Centred.Resolve().DirectionZ + 1.0) < 1.0e-12,
           "[-] `46` §3's negative third axis");

    // 📐 The projection already applies `ClipCoordinateSignum`, so a pointer above the centre must cast upward in
    //    document space. A second inversion here would only be visible on this axis, which reads as a camera
    //    that is subtly mis-aimed rather than as an inversion.
    const Slate::Deliver<Slate::ProjectedRay> Upper =
        Slate::ProjectPointerRay(Camera, 256.0, 64.0, 512u, 512u);

    Report("The display's downward coordinate is not inverted twice",
           Upper.Resolved && Upper.Resolve().DirectionY > 0.0,
           "[-] a pointer above the centre casts upward");

    Slate::PointerIntersection Picking;

    Slate::AxisSpace Domain;

    std::vector<Slate::DomainExtent> Placements(1);
    Placements[0].MinimumX       = 0.55;  Placements[0].MinimumY     = 0.55;
    Placements[0].MaximumX    = 0.95;  Placements[0].MaximumY  = 0.95;
    Placements[0].PlacementIndex = 0u;    Placements[0].SequenceIndex = 1u;

    Domain.ConstructAxes(Placements);

    Slate::AcceptedSurface Surfaced;
    Surfaced.Owner          = Subject;
    Surfaced.Imported          = &Imported;
    Surfaced.CornerCoordinates = &Coordinates;
    Surfaced.Placements        = &Domain;

    Report("A surface is accepted", Picking.Accept(Surfaced).Resolved, "[-] with its coordinate run");

    Slate::AcceptedSurface Mismatched = Surfaced;
    std::vector<Slate::DomainCoordinate> Short(2);
    Mismatched.CornerCoordinates = &Short;

    Report("A coordinate run of the wrong length is rejected",
           !Picking.Accept(Mismatched).Resolved,
           "[-] confirmed at admission, never at the hit");

    Slate::RegistrationIndex Subsets;
    Slate::PlacementIndex  Declared;

    Slate::PlacementSpecification Placing;
    Placing.Owner                               = Subject;
    Placing.PlacingTransform.Translation.PositionX = 0.75;
    Placing.PlacingTransform.Translation.PositionY = 0.75;
    Placing.PlacingTransform.ScaleX                = 0.4;
    Placing.PlacingTransform.ScaleY                = 0.4;

    Discard(Declared.Declare(Placing));

    const Slate::ResolvedPointer Met =
        Picking.Resolve(Centred.Resolve(), Outer, Subsets, Declared);

    Report("The ray resolves the owner",
           Met.Resolved && Met.Owner == Subject,
           "[-] one traversal, the whole tuple");

    Report("The domain position interpolates to the centre",
           Met.DomainResolved
        && std::fabs(Met.DomainX  - 0.5) < 1.0e-9
        && std::fabs(Met.DomainY - 0.5) < 1.0e-9,
           "[-] barycentric, over the corners' own coordinates");

    Report("The face orientation faces the camera",
           Met.Orientation.DirectionZ > 0.0f,
           "[-] the flat perpendicular `78` builds a plane from");

    Report("No placement contains the centre",
           !Met.PlacementResolved,
           "[-] the extent is elsewhere");

    // 📐 A pointer aimed at domain (0.75, 0.75) is aimed at document (0.5, 0.5) on this square, which projects
    //    to a pixel offset from centre by the perspective scale at ten millimetres.
    const double OffsetPixels = 256.0 + 0.5 * 256.0 / (10.0 * std::tan(22.5 * Slate::Pi / 180.0));

    const Slate::Deliver<Slate::ProjectedRay> AtPlacement =
        Slate::ProjectPointerRay(Camera, OffsetPixels, 512.0 - OffsetPixels, 512u, 512u);

    const Slate::ResolvedPointer Placed =
        Picking.Resolve(AtPlacement.Resolve(), Outer, Subsets, Declared);

    Report("A placement resolves before its carrying surface",
           Placed.PlacementResolved && Placed.PlacementIndex == 0u,
           "[-] `74` §3 precedence 1, confirmed through the source square");

    Report("The owner is reported beside the placement",
           Placed.Resolved && Placed.Owner == Subject,
           "[-] `78` still needs the surface's orientation");

    const std::vector<Slate::OwnerIdentity> Contained =
        Picking.ResolveExtent(Camera, 0.0, 0.0, 512.0, 512.0, 512u, 512u, true, Outer, Subsets);

    Report("A marquee over the whole display contains the owner",
           Contained.size() == 1u,
           "[-] one traversal over the extent");

    const std::vector<Slate::OwnerIdentity> Cornered =
        Picking.ResolveExtent(Camera, 0.0, 0.0, 16.0, 16.0, 512u, 512u, false, Outer, Subsets);

    Report("A marquee in a corner touches nothing",
           Cornered.empty(),
           "[-] classified exactly against the six planes, not against the bound");

    Discard(Subsets.Register(Subject, Slate::SubsetSubject::Lock));

    Report("A locked owner is neither picked nor registered",
           !Picking.Resolve(Centred.Resolve(), Outer, Subsets, Declared).Resolved
        && Picking.ResolveExtent(Camera, 0.0, 0.0, 512.0, 512.0, 512u, 512u, false, Outer, Subsets).empty(),
           "[-] excluded before descent — `40` §3");

    Discard(Subsets.Unenrol(Subject, Slate::SubsetSubject::Lock));

    Report("Withdrawal removes the sources",
           Picking.Withdraw(Subject).Resolved && Picking.AcceptedCount() == 0u,
           "[-] and a second withdrawal refuses");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE TOOLS
//------------------------------------------------------------------------------------------------------------------------

void VerifyTools()
{
    std::printf("ToolSequence\n");

    Slate::ToolSequence Held;

    Slate::ToolSpecification Texturing;
    Texturing.Identity  = "Brushed";
    Texturing.Current = "Brushed";
    Texturing.Reserved   = Slate::PointerPrecedence::Stroke;
    Texturing.Previewed = Slate::PreviewSubject::Impression;
    Texturing.Recorded  = Slate::TransactionSubject::Dragged;

    Slate::PropertyDeclaration Strength;
    Strength.Identity                 = "Strength";
    Strength.Current                = "Strength";
    Strength.Measured                 = Slate::PropertyMeasure::Magnitude;
    Strength.LowerMagnitude           = 0.0;
    Strength.UpperMagnitude           = 1.0;
    Strength.BoundsDeclared           = true;
    Strength.Defaulted.Measured       = Slate::PropertyMeasure::Magnitude;
    Strength.Defaulted.MagnitudeHeld  = 1.0;

    Discard(Texturing.Parameters.Declare(Strength));

    const Slate::Deliver<std::uint32_t> Declared = Held.Tools().Declare(Texturing);

    Report("A tool is declared", Declared.Resolved, "[-] with its parameters");

    Report("A tool parameter is a declaration, not panel code",
           Declared.Resolved
        && Held.Tools().Resolve(Declared.Resolve()).Resolve()->Parameters.Declarations().size() == 1u,
           "[-] `76` §4: any tool presents without the panel knowing which");

    Report("A repeated identity is rejected",
           !Held.Tools().Declare(Texturing).Resolved,
           "[-] resolution by identity must answer one tool");

    Report("Nothing is active before a tool is declared active",
           !Held.ActiveTool().Resolved && !Held.ActiveBrush().Resolved,
           "[-] refuses rather than resolving the first declared");

    Report("A tool activates",
           Held.DeclareTool(Declared.Resolve()).Resolved
        && Held.ActiveTool().Resolve()->Reserved == Slate::PointerPrecedence::Stroke,
           "[-] and carries its declared precedence");

    Slate::ColourSpecification Spaceless;
    Spaceless.RedCoordinate = 1.0;

    Report("A colour with no space is rejected",
           !Held.DeclareColour(Spaceless).Resolved,
           "[-] `36` §1: no bare triple reaches a stroke");

    Slate::ColourSpecification Working = Spaceless;
    Working.SpaceIdentity = Slate::WorkingSpaceIdentity;

    Report("A colour carrying its space is accepted",
           Held.DeclareColour(Working).Resolved
        && Held.Colour().SpaceIdentity == Slate::WorkingSpaceIdentity,
           "[-] scene-referred, per `36` §6");

    Report("The brush store is here and starts empty",
           Held.Brushes().DeclaredCount() == 0u,
           "[-] `58` §7's per-application store");

    const std::uint32_t BrushIndex = Held.Brushes().Declare("Round", "Default").Resolve();

    Report("A brush activates",
           Held.DeclareBrush(BrushIndex).Resolved && Held.ActiveBrush().Resolved,
           "[-] the ordinal is what `22` resolves against");

    Report("An overlay is presented by declaration",
           Held.DeclareOverlay(Slate::OverlaySubject::Wireframe, true).Resolved
        && Held.OverlayActive(Slate::OverlaySubject::Wireframe)
        && !Held.OverlayActive(Slate::OverlaySubject::GroundLattice),
           "[-] read by `80`, stored in no document");

    Report("Arbitration prefers the interface",
           Held.Arbitrate(true, true, true) == Slate::PointerPrecedence::Interface,
           "[-] `14` §4.2's four levels, in order");

    Report("Arbitration falls to the workspace",
           Held.Arbitrate(false, false, false) == Slate::PointerPrecedence::Workspace,
           "[-] picking and navigation");

    Slate::ResolvedPointer Opened;
    Opened.Resolved     = true;
    Opened.DomainX  = 0.25;
    Opened.DomainY = 0.75;

    Report("A capture opens against a pick",
           Held.OpenCapture(Slate::PointerPrecedence::Stroke, Opened).Resolved
        && Held.Capture().Opened.DomainX == 0.25,
           "[-] `78` §2 fixes its plane from this");

    // 🔴 The property the whole capture exists for: a stronger claimant does not steal it, and arbitration
    //    answers the holder unconditionally while it stands.
    Report("A stronger claimant does not steal the capture",
           !Held.OpenCapture(Slate::PointerPrecedence::Interface, Opened).Resolved
        && Held.Arbitrate(true, true, true) == Slate::PointerPrecedence::Stroke,
           "[-] a stroke is not stopped by a panel it crosses");

    Report("Releasing is explicit",
           Held.ReleaseCapture().Resolved
        && !Held.Capture().CaptureDeclared
        && !Held.ReleaseCapture().Resolved,
           "[-] never a consequence of the pointer moving");

    Report("Arbitration resumes after release",
           Held.Arbitrate(true, false, false) == Slate::PointerPrecedence::Interface,
           "[-] once, before the next capture is taken");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE RADIANCE CHAIN
//------------------------------------------------------------------------------------------------------------------------

// 📝 The four documents between `18` and the display, verified as one chain rather than four components, because
//    what they share is an ordering and every one of the four defects below is an ordering written backwards.
void VerifyRadianceChain()
{
    std::printf("Radiance chain\n");

    Slate::TransmissionSequence Transmitting;

    Slate::MaterialSpecification Glass;
    Glass.DeclareReflectance(Slate::ReflectanceSelection::Transmissive);

    Slate::MaterialSpecification Foliage;
    Foliage.DeclareReflectance(Slate::ReflectanceSelection::Standard);
    Foliage.DeclareCutoutRegistration(true);
    Foliage.DeclareCutoutThreshold(0.4);

    Report("Transmissive resolves here",
           Slate::BehaviourOf(Glass) == Slate::TransmissionBehaviour::Transmissive,
           "[-] `62` §2's third row");

    Report("Cutout resolves at `16`",
           Slate::BehaviourOf(Foliage) == Slate::TransmissionBehaviour::Cutout,
           "[-] a leaf card is opaque with a hole in it");

    Report("The cutout threshold is the material's own",
           Slate::CoverageResolved(Foliage, 0.5) && !Slate::CoverageResolved(Foliage, 0.3),
           "[-] never global — `62` §2");

    // 📐 Four fragments incoming farthest-first into a column of `TransmissionDepth`. The column must end up
    //    nearest-first regardless of arrival order, which is the ordering the whole document rests on.
    Slate::TransmissionColumn Column;

    bool OrderHeld = true;

    for (std::uint32_t Index = 0u; Index < 6u; ++Index)
    {
        const double Depth = 0.1 + static_cast<double>(Index) * 0.1;

        Slate::TransmissionFragment Incoming;
        Incoming.Depth       = Depth;
        Incoming.DepthKey    = Slate::ProjectTransmissionKey(Depth);
        Incoming.SurfaceWord = Slate::PackTransmissionSurface(Index, 1u);

        Transmitting.Insert(Column, Incoming, 0.0);
    }

    for (std::uint32_t Index = 1u; Index < Column.HeldCount; ++Index)
    {
        if (Column.Held[Index - 1u].Depth <= Column.Held[Index].Depth)
            OrderHeld = false;
    }

    Report("The column is nearest first", OrderHeld, "[-] whatever order the fragments arrived in");

    Report("The ceiling discards the farthest",
           Column.HeldCount == Slate::TransmissionDepth
        && Column.TruncatedCount == 6u - Slate::TransmissionDepth
        && Column.Held[Column.HeldCount - 1u].Depth > 0.1,
           "[-] `62` §3.1 — never the nearest");

    // 🔴 A fragment behind the resolved opaque depth is discarded. Under the reversed convention "behind" is a
    //    lesser coordinate, so this is the one comparison whose inversion fills the column with the invisible.
    Slate::TransmissionColumn Occluded;

    Slate::TransmissionFragment Behind;
    Behind.Depth       = 0.2;
    Behind.DepthKey    = Slate::ProjectTransmissionKey(0.2);
    Behind.SurfaceWord = Slate::PackTransmissionSurface(9u, 0u);

    Report("A fragment behind the opaque depth is discarded",
           !Transmitting.Insert(Occluded, Behind, 0.5) && Occluded.HeldCount == 0u,
           "[-] it is not visible and would amend a pixel it does not reach");

    Slate::SpecularProjection Reflecting;

    Slate::ReflectionSpecification Tracing;

    Report("The trace declares", Reflecting.Declare(Tracing).Resolved, "[-] the four bounds");

    Slate::ReflectionSpecification Thirded = Tracing;
    Thirded.ExtentDivisor                  = 3u;

    Report("A third of the display extent is rejected",
           !Reflecting.Declare(Thirded).Resolved,
           "[-] `08` §2 claims the target at half extent and nowhere else");

    // 🔴 `30` §1: at a weight of nothing the composite is the identity, which is what makes every one of the
    //    four failure rows free and invisible. This is the single most consequential line in that document.
    const double Current[3] = { 0.4, 0.5, 0.6 };
    const double PreAdded[3] = { 0.1, 0.1, 0.1 };

    Slate::TracedReflection Failed;
    double                  Resolved[3] = { 0.0, 0.0, 0.0 };

    Reflecting.Compose(Current, PreAdded, Failed, Resolved);

    Report("A failed trace composes to a no-op",
           Resolved[0] == Current[0] && Resolved[1] == Current[1] && Resolved[2] == Current[2],
           "[-] the subtraction and the addition cancel");

    Slate::TracedReflection Succeeded;
    Succeeded.Weight       = 1.0;
    Succeeded.Resolved     = true;
    Succeeded.Component[0] = 0.9;
    Succeeded.Component[1] = 0.9;
    Succeeded.Component[2] = 0.9;

    Reflecting.Compose(Current, PreAdded, Succeeded, Resolved);

    Report("A resolved trace swaps rather than adds",
           std::fabs(Resolved[0] - (Current[0] - PreAdded[0] + 0.9)) < 1.0e-12,
           "[-] `18`'s ambient specular is not counted twice");

    Slate::SampleIntegrator Accumulating;

    Report("The rejection declares",
           Accumulating.Declare(Slate::RejectionSpecification{}).Resolved,
           "[-] both bounds and the ceiling");

    // 🔴 `64` §6 and §8: before one rotation has accumulated, no history describes anything, so every
    //    classification is rejected off the extent whatever the owner and the depth agree about.
    Report("No history is read before one exists",
           !Accumulating.HistoryReadable()
        && Accumulating.Classify(0.5, 0.5, 7u, 7u, 0.5, 0.5) == Slate::RejectionSubject::OffExtent
        && Accumulating.Classify(0.5, 0.5, 7u, 8u, 0.5, 0.5) == Slate::RejectionSubject::OffExtent,
           "[-] a refusal writes the incoming sample whole and the count starts at one");

    // 📝 A rotation that accumulated anything at all leaves a history the next one may read, which is what
    //    `DeclareRotation` raises — never a separate admission the caller could forget.
    Accumulating.DeclareRotation(1u, 1u, 0u, 1u);

    Report("A rotation that accumulated leaves a history",
           Accumulating.HistoryReadable(),
           "[-] the second rotation onward may reproject one");

    Report("A different owner refuses",
           Accumulating.Classify(0.5, 0.5, 8u, 7u, 0.5, 0.5) == Slate::RejectionSubject::OwnerDiffers,
           "[-] `16` §4.1's resolution and never the partition identity");

    Report("A reprojection off the extent refuses",
           Accumulating.Classify(1.5, 0.5, 7u, 7u, 0.5, 0.5) == Slate::RejectionSubject::OffExtent,
           "[-] the leading edge of a moving camera has no history at all");

    Report("The same surface accepts",
           Accumulating.Classify(0.5, 0.5, 7u, 7u, 0.5, 0.5) == Slate::RejectionSubject::Accepted,
           "[-] one owner, one depth, one extent");

    Slate::DisplayProjection Displaying;

    Report("The tone line declares",
           Displaying.Declare(Slate::ExposureSpecification{},
                              Slate::ToneSpecification{},
                              Slate::EncodeSpecification{}).Resolved,
           "[-] the exposure, the compression and the two spaces as one admission");

    // 🔴 `66` §4 and §8: the display space is queried or declared and never assumed to be the working space.
    //    Accepting the two as one produces an image correct on exactly the machine it was authored on.
    Slate::EncodeSpecification Assumed;
    Assumed.Display = Slate::DeclaredWorkingSpace();

    Report("A display space that is the working space is rejected",
           !Displaying.Declare(Slate::ExposureSpecification{}, Slate::ToneSpecification{}, Assumed).Resolved,
           "[-] `36` §9 — and it is silent everywhere else");

    Slate::ToneSpecification Unbounded;
    Unbounded.WhiteMagnitude = 0.0;

    Report("A white magnitude of nothing is rejected",
           !Displaying.Declare(Slate::ExposureSpecification{},
                               Unbounded,
                               Slate::EncodeSpecification{}).Resolved,
           "[-] it would compress the whole scene to nothing");

    // 📐 `66` §2: exposure is a scale on radiance and a doubling per stop, applied before the compression.
    Report("The exposure scale is a doubling per stop",
           std::fabs(Displaying.ExposureScale() - 1.0) < 1.0e-12,
           "[-] a declared exposure of nought scales radiance by one");
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE HOST
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::printf("Slate — headless bring-up\n\n");

    VerifyMathematics();
    std::printf("\n");

    VerifyClassifiers();
    std::printf("\n");

    VerifyReporting();
    std::printf("\n");

    VerifyWork();
    std::printf("\n");

    VerifyColour();
    std::printf("\n");

    VerifyDocument();
    std::printf("\n");

    VerifyProperties();
    std::printf("\n");

    VerifyTopology();
    std::printf("\n");

    VerifyMaterials();
    std::printf("\n");

    VerifyVector();
    std::printf("\n");

    VerifyCamera();
    std::printf("\n");

    VerifyIlluminants();
    std::printf("\n");

    VerifySubdivision();
    std::printf("\n");

    VerifyPointer();
    std::printf("\n");

    VerifyTools();
    std::printf("\n");

    VerifyPartition();
    std::printf("\n");

    VerifyIntake();
    std::printf("\n");

    VerifyStroke();
    std::printf("\n");

    VerifySchedule();
    std::printf("\n");

    VerifyParity();
    std::printf("\n");

    VerifyAtmosphere();
    std::printf("\n");

    VerifyRadianceChain();
    std::printf("\n");

    if (RejectedCount == 0)
    {
        std::printf("every check held\n");
        return 0;
    }

    std::printf("%d checks rejected\n", RejectedCount);
    return RejectedCount;
}
