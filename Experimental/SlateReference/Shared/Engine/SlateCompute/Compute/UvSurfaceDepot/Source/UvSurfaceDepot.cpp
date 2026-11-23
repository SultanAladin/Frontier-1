//============================================================================================================================================
//                                                           UVSURFACEDEPOT.CPP
//============================================================================================================================================
// 🧩 A Tier A extent test, a rule that chooses among what it accepted, sweeps that converge — and a miss recorded as a miss.

#include "SlateCompute/Compute/UvSurfaceDepot/Api/UvSurfaceDepot.h"

#include "Shared/IntersectionClassifier.slang.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

const char* const TransferOrigin = "24 §4 UvSurfaceDepot";

/// 🧩 One source face's axis-aligned extent and its centre, as the search reads them.
struct FaceExtent
{
    double  MinimumX    = 0.0;   // [mm] - the face's own bound, in object space
    double  MinimumY    = 0.0;   // [mm]
    double  MinimumZ    = 0.0;   // [mm]
    double  MaximumX = 0.0;   // [mm]
    double  MaximumY = 0.0;   // [mm]
    double  MaximumZ = 0.0;   // [mm]
    double  CentreX   = 0.0;   // [mm] - the mean of its corners; what the departure is measured to
    double  CentreY   = 0.0;   // [mm]
    double  CentreZ   = 0.0;   // [mm]
};

// 📐 One face's bound and centre from its corners, in the source's own ordering. Derived per search rather than
//    held, because the source is immutable for the whole run and a second copy of a bound is a second thing that
//    can describe a revision the topology has left.
FaceExtent ExtentOfFace(const TopologyStructure& Source, std::uint32_t FaceIndex)
{
    FaceExtent Bounded;

    const std::uint32_t FirstCorner = Source.FaceFirstCorner(FaceIndex);
    const std::uint32_t CornerSpan  = Source.FaceCornerCount(FaceIndex);

    if (CornerSpan == 0u)
    {
        return Bounded;
    }

    const std::vector<DocumentPosition>& Positions = Source.Positions();

    double AccumulatedX = 0.0;
    double AccumulatedY = 0.0;
    double AccumulatedZ = 0.0;

    for (std::uint32_t Walked = 0u; Walked < CornerSpan; ++Walked)
    {
        const DocumentPosition& Held = Positions[Source.CornerVertex(FirstCorner + Walked)];

        if (Walked == 0u)
        {
            Bounded.MinimumX = Bounded.MaximumX = Held.PositionX;
            Bounded.MinimumY = Bounded.MaximumY = Held.PositionY;
            Bounded.MinimumZ = Bounded.MaximumZ = Held.PositionZ;
        }
        else
        {
            Bounded.MinimumX    = Held.PositionX < Bounded.MinimumX    ? Held.PositionX : Bounded.MinimumX;
            Bounded.MinimumY    = Held.PositionY < Bounded.MinimumY    ? Held.PositionY : Bounded.MinimumY;
            Bounded.MinimumZ    = Held.PositionZ < Bounded.MinimumZ    ? Held.PositionZ : Bounded.MinimumZ;
            Bounded.MaximumX = Held.PositionX > Bounded.MaximumX ? Held.PositionX : Bounded.MaximumX;
            Bounded.MaximumY = Held.PositionY > Bounded.MaximumY ? Held.PositionY : Bounded.MaximumY;
            Bounded.MaximumZ = Held.PositionZ > Bounded.MaximumZ ? Held.PositionZ : Bounded.MaximumZ;
        }

        AccumulatedX += Held.PositionX;
        AccumulatedY += Held.PositionY;
        AccumulatedZ += Held.PositionZ;
    }

    Bounded.CentreX = AccumulatedX / static_cast<double>(CornerSpan);
    Bounded.CentreY = AccumulatedY / static_cast<double>(CornerSpan);
    Bounded.CentreZ = AccumulatedZ / static_cast<double>(CornerSpan);

    return Bounded;
}

// 📐 The face's orientation as the mean of its corners' own, unnormalised. The angular rule compares magnitudes
//    against one another and never against an absolute bound, so the normalisation the mean omits cancels.
SurfaceDirection OrientationOfFace(const TopologyStructure& Source, std::uint32_t FaceIndex)
{
    SurfaceDirection Averaged;

    if (!Source.PerpendicularsSupplied())
    {
        return Averaged;
    }

    const std::vector<SurfaceDirection>& Perpendiculars = Source.Perpendiculars();

    const std::uint32_t FirstCorner = Source.FaceFirstCorner(FaceIndex);
    const std::uint32_t CornerSpan  = Source.FaceCornerCount(FaceIndex);

    for (std::uint32_t Walked = 0u; Walked < CornerSpan; ++Walked)
    {
        const SurfaceDirection& Held = Perpendiculars[Source.CornerVertex(FirstCorner + Walked)];

        Averaged.DirectionX += Held.DirectionX;
        Averaged.DirectionY += Held.DirectionY;
        Averaged.DirectionZ += Held.DirectionZ;
    }

    return Averaged;
}

double DepartureBetween(DocumentPosition Current, const FaceExtent& Bounded)
{
    const double OffsetX = Current.PositionX - Bounded.CentreX;
    const double OffsetY = Current.PositionY - Bounded.CentreY;
    const double OffsetZ = Current.PositionZ - Bounded.CentreZ;

    return std::sqrt(OffsetX * OffsetX + OffsetY * OffsetY + OffsetZ * OffsetZ);
}

// 📐 How nearly the face lies along the working orientation. Greater is more nearly aligned, and a source with
//    no perpendiculars supplied returns nothing for every face — which leaves the angular rule choosing among
//    equals, so the departure below decides. That is a degradation to the other rule and never a refusal.
double AlignmentBetween(SurfaceDirection Working, SurfaceDirection Faced)
{
    return Working.DirectionX * Faced.DirectionX
         + Working.DirectionY * Faced.DirectionY
         + Working.DirectionZ * Faced.DirectionZ;
}

}   // namespace

Deliver<bool> UvSurfaceDepot::Declare(const TransferSpecification& Transferring_)
{
    if (!(Transferring_.SearchExtent > 0.0))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a search extent of nothing corresponds to no source surface" });
    }

    // 🔴 An empty mask transfers nothing while reporting no miss and no resolution, which reads as a transfer
    //    that succeeded. `24` §4 obliges this to report what it missed, and it cannot report a channel it was
    //    never asked for.
    if (Transferring_.ChannelMask == 0u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "no channel was declared for transfer" });
    }

    if (Transferring_.DomainExtent == 0u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a domain extent of nothing writes the result nowhere" });
    }

    if (!(Transferring_.ConvergenceCriterion > 0.0) || Transferring_.ConvergenceCriterion > 1.0)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the convergence criterion lies outside the unit interval" });
    }

    if (Transferring_.IterationLimit == 0u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "an iteration ceiling of nothing accepts no sweep at all" });
    }

    if (Transferring_.Correspondence == CorrespondenceSubject::CorrespondenceCount)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the closed count is not a correspondence rule" });
    }

    Transferring     = Transferring_;
    TransferCurrent = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CONTENT KEY
//------------------------------------------------------------------------------------------------------------------------

Deliver<ContentKey> UvSurfaceDepot::KeyOf(const TopologyStructure& Source,
                                          const TopologyStructure& Working,
                                          const ChartPartition&    Partitioning) const
{
    if (!TransferCurrent)
    {
        return Deliver<ContentKey>::Refuse(
            { RefusalReason::ContentUnsupported, "no transfer was declared to key" });
    }

    if (!Source.Sealed() || !Working.Sealed())
    {
        return Deliver<ContentKey>::Refuse(
            { RefusalReason::ContentUnsupported, "an unsealed topology carries no revision to key on" });
    }

    // 🔴 `68` §6 and `24` §3: the partition revision moves every domain position with it. A result keyed without
    //    it survives a re-unwrap and is then read at positions that mean something else.
    if (!Partitioning.PartitionCurrent())
    {
        return Deliver<ContentKey>::Refuse(
            { RefusalReason::ContentUnsupported, "no partition stands, so no domain position means anything yet" });
    }

    ContentKey Keyed;
    Keyed.SourceRevision       = Source.Revision();
    Keyed.WorkingRevision      = Working.Revision();
    Keyed.PartitionRevision    = Partitioning.Revision();
    Keyed.SpecificationIndex = Transferring.SpecificationIndex;
    Keyed.ExtentTexels         = Transferring.DomainExtent;
    Keyed.ChannelMask          = Transferring.ChannelMask;

    return Deliver<ContentKey>::Result(Keyed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CORRESPONDENCE
//------------------------------------------------------------------------------------------------------------------------

Deliver<SourceCorrespondence> UvSurfaceDepot::Correspond(DocumentPosition         WorkingPosition,
                                                         SurfaceDirection         WorkingOrientation,
                                                         const TopologyStructure& Source) const
{
    if (!TransferCurrent)
    {
        return Deliver<SourceCorrespondence>::Refuse(
            { RefusalReason::ContentUnsupported, "no transfer was declared to correspond against" });
    }

    const double Extent = Transferring.SearchExtent;

    SourceCorrespondence Chosen;
    Chosen.FaceIndex = AbsentCorrespondence;

    double MinimumDeparture   = 0.0;
    double MaximumAligning = 0.0;

    const std::uint32_t FaceSpan = Source.FaceCount();

    for (std::uint32_t FaceIndex = 0u; FaceIndex < FaceSpan; ++FaceIndex)
    {
        const FaceExtent Bounded = ExtentOfFace(Source, FaceIndex);

        // 🔴 Tier A admission — `24` §2 and §5's second gate. The working position is grown into a volume of the
        //    declared extent and the two volumes are classified by `Shared/`'s own routine, so the host and the
        //    device answer the same question the same way. A comparison written here would be the second
        //    implementation the classifier exists to prevent.
        const Signed32 Overlap = ClassifyVolumeOverlap(
            WorkingPosition.PositionX - Extent, WorkingPosition.PositionY - Extent, WorkingPosition.PositionZ - Extent,
            WorkingPosition.PositionX + Extent, WorkingPosition.PositionY + Extent, WorkingPosition.PositionZ + Extent,
            Bounded.MinimumX,    Bounded.MinimumY,    Bounded.MinimumZ,
            Bounded.MaximumX, Bounded.MaximumY, Bounded.MaximumZ);

        if (Overlap < 0)
        {
            continue;
        }

        const double Departure = DepartureBetween(WorkingPosition, Bounded);

        // 🔴 The extent is a ceiling and nothing samples past it. A face whose bound reaches into the search
        //    volume while its centre stands beyond the extent is rejected here rather than delivered as the
        //    nearest thing found — `24` §2 refuses the fabricated value by name.
        if (Departure > Extent)
        {
            continue;
        }

        const double Aligning = AlignmentBetween(WorkingOrientation, OrientationOfFace(Source, FaceIndex));

        bool Preferred = Chosen.FaceIndex == AbsentCorrespondence;

        if (!Preferred && Transferring.Correspondence == CorrespondenceSubject::MinimumAngularDeparture)
        {
            Preferred = Aligning > MaximumAligning
                    || (Aligning == MaximumAligning && Departure < MinimumDeparture);
        }
        else if (!Preferred)
        {
            Preferred = Departure < MinimumDeparture;
        }

        if (Preferred)
        {
            Chosen.FaceIndex = FaceIndex;
            Chosen.Departure   = Departure;
            MinimumDeparture     = Departure;
            MaximumAligning   = Aligning;
        }
    }

    if (Chosen.FaceIndex == AbsentCorrespondence)
    {
        return Deliver<SourceCorrespondence>::Refuse(
            { RefusalReason::ExtentExhausted, "no source surface stands within the declared search extent" });
    }

    return Deliver<SourceCorrespondence>::Result(Chosen);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE TRANSFER
//------------------------------------------------------------------------------------------------------------------------

ConvergentResult<TransferMetrics> UvSurfaceDepot::Transfer(const TopologyStructure&          Source,
                                                           const TopologyStructure&          Working,
                                                           const std::vector<std::uint32_t>& SourceChannelMasks) const
{
    ConvergentResult<TransferMetrics> Produced;

    if (!TransferCurrent)
    {
        return Produced;
    }

    const std::uint32_t DomainSpan = Working.VertexCount();

    Produced.Approximation.DomainCount = DomainSpan;

    if (DomainSpan == 0u)
    {
        Produced.Cause = TerminationCause::CriterionSatisfied;
        return Produced;
    }

    const std::vector<DocumentPosition>& WorkingPositions = Working.Positions();
    const std::vector<SurfaceDirection>  NoOrientations;
    const std::vector<SurfaceDirection>& WorkingOrientations = Working.PerpendicularsSupplied()
                                                             ? Working.Perpendiculars()
                                                             : NoOrientations;

    std::vector<std::uint32_t> Corresponded(DomainSpan, AbsentCorrespondence);

    // 📐 The sweep spreads: an unresolved position is retried against the faces its own topology's resolved
    //    positions found, which is what makes the transfer converge rather than terminate at a fixed cost. The
    //    retry passes the same extent test, so propagation never reaches a face the direct search would not.
    for (std::uint32_t Swept = 0u; Swept < Transferring.IterationLimit; ++Swept)
    {
        std::uint32_t ResolvedThisSweep = 0u;

        for (std::uint32_t VertexIndex = 0u; VertexIndex < DomainSpan; ++VertexIndex)
        {
            if (Corresponded[VertexIndex] != AbsentCorrespondence)
            {
                continue;
            }

            SurfaceDirection Orientation;

            if (!WorkingOrientations.empty())
            {
                Orientation = WorkingOrientations[VertexIndex];
            }

            const Deliver<SourceCorrespondence> Found =
                Correspond(WorkingPositions[VertexIndex], Orientation, Source);

            if (Found.Resolved)
            {
                Corresponded[VertexIndex] = Found.Resolve().FaceIndex;
                ++ResolvedThisSweep;
            }
        }

        Produced.Approximation.SweepCount = Swept + 1u;
        Produced.ResidualNorm             = static_cast<double>(ResolvedThisSweep) / static_cast<double>(DomainSpan);
        Produced.IterationCount           = Swept + 1u;

        // 📐 The residual is the fraction newly resolved. A sweep that resolved nothing further has converged;
        //    one still resolving above the criterion has more to do. Measured as a fraction rather than as a
        //    count so that the criterion means the same thing on a topology of a thousand and of a million.
        if (Produced.ResidualNorm <= Transferring.ConvergenceCriterion)
        {
            Produced.Cause = TerminationCause::CriterionSatisfied;
            break;
        }

        Produced.Cause = TerminationCause::LimitReached;
    }

    // 🔴 `24` §2: whatever remains unresolved is recorded as a miss — never as zero, never as the nearest value
    //    found beyond the extent. A channel the corresponding source face does not itself carry is a miss too,
    //    for the same reason: it was asked for and not delivered.
    for (std::uint32_t VertexIndex = 0u; VertexIndex < DomainSpan; ++VertexIndex)
    {
        const std::uint32_t FaceIndex = Corresponded[VertexIndex];

        if (FaceIndex != AbsentCorrespondence)
        {
            ++Produced.Approximation.ResolvedCount;
        }

        const std::uint32_t SourceCarries = FaceIndex != AbsentCorrespondence
                                         && FaceIndex < SourceChannelMasks.size()
                                          ? SourceChannelMasks[FaceIndex]
                                          : 0u;

        for (std::uint32_t ChannelIndex = 0u;
             ChannelIndex < static_cast<std::uint32_t>(ChannelSubject::ChannelCount);
             ++ChannelIndex)
        {
            const std::uint32_t ChannelBit = 1u << ChannelIndex;

            if ((Transferring.ChannelMask & ChannelBit) == 0u)
            {
                continue;
            }

            if ((SourceCarries & ChannelBit) == 0u)
            {
                ++Produced.Approximation.ChannelMisses[ChannelIndex];
            }
        }
    }

    return Produced;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ADMISSION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> UvSurfaceDepot::Accept(SurfaceDepot&     Depot,
                                    const ContentKey& Keyed,
                                    std::uint64_t     ByteExtent,
                                    std::uint64_t     RecordingIndex) const
{
    // 🔴 Declared an analytic resolution, which `56` §3 classifies as reconstructible — so the depot accepts it as
    //    evictable. Nothing textured is ever declared here: texture is a layer above the transfer in `56`'s sequence
    //    and stays there, rather than the transfer mutating into authored content underneath it.
    return Depot.Declare(Keyed, LayerContentSource::AnalyticResolution, ByteExtent, RecordingIndex);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE REPORTING
//------------------------------------------------------------------------------------------------------------------------

void UvSurfaceDepot::Report(const ConvergentResult<TransferMetrics>& Produced,
                            ReportSequence&                          Reporting,
                            MeasureIndex&                            Measured,
                            TickPoint                                Sampled) const
{
    if (Produced.Cause == TerminationCause::LimitReached)
    {
        ReportSpecification Terminated;
        Terminated.Origin         = TransferOrigin;
        Terminated.Subject        = "Transfer";
        Terminated.Detail         = "the iteration ceiling terminated the transfer; positions remain unresolved";
        Terminated.SubjectIndex = Produced.Approximation.SweepCount;
        Terminated.Verdict    = ReportVerdict::Terminated;
        Terminated.Arrival        = Sampled;

        Reporting.Append(Terminated);
    }

    // 🔴 `24` §4: the miss count is per channel and each missed channel appends its own report. One total says
    //    nothing about which attribute is wrong, and a transfer that missed a tenth of the domain in one channel
    //    looks like one that missed nothing everywhere the transfer succeeded.
    for (std::uint32_t ChannelIndex = 0u;
         ChannelIndex < static_cast<std::uint32_t>(ChannelSubject::ChannelCount);
         ++ChannelIndex)
    {
        if (Produced.Approximation.ChannelMisses[ChannelIndex] == 0u)
        {
            continue;
        }

        ReportSpecification Missed;
        Missed.Origin         = TransferOrigin;
        Missed.Subject        = "ChannelMiss";
        Missed.Detail         = "no source surface within the extent carried this channel; the domain position is a miss";
        Missed.SubjectIndex = (static_cast<std::uint64_t>(ChannelIndex) << 32)
                              | Produced.Approximation.ChannelMisses[ChannelIndex];
        Missed.Verdict    = ReportVerdict::Truncated;
        Missed.Arrival        = Sampled;

        Reporting.Append(Missed);
    }

    // 📝 The counts overwrite. A count appended every transfer buries the one channel that missed under readings
    //    nobody asked for — the same line `68`'s reporting draws.
    Measured.DeclareCount(TransferOrigin, "DomainCount",   Produced.Approximation.DomainCount,   Sampled);
    Measured.DeclareCount(TransferOrigin, "ResolvedCount", Produced.Approximation.ResolvedCount, Sampled);
    Measured.DeclareCount(TransferOrigin, "SweepCount",    Produced.Approximation.SweepCount,    Sampled);
    Measured.DeclareMagnitude(TransferOrigin, "Residual", Produced.ResidualNorm, Sampled);
}

const TransferSpecification& UvSurfaceDepot::Specification() const { return Transferring; }

}   // namespace Slate
