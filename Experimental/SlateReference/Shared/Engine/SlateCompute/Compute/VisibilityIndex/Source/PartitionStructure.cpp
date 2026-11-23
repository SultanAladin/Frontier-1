//============================================================================================================================================
//                                                          PARTITIONSTRUCTURE.CPP
//============================================================================================================================================
// 🧩 The growth front that walks `38`'s adjacency, the cone it accumulates, and the identities `42` issues against the result.

#include "SlateCompute/Compute/VisibilityIndex/Api/PartitionStructure.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   FACE ORIENTATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The material one face carries. A topology whose source declared no enrollment carries one material for the
//    whole surface, and reading past the end of an undeclared run is how that surface acquires a partition split
//    at every face instead.
std::uint32_t MaterialOfFace(const TopologyStructure& Imported, std::uint32_t FaceIndex)
{
    const std::vector<std::uint32_t>& Registration = Imported.MaterialRegistration();

    return FaceIndex < static_cast<std::uint32_t>(Registration.size()) ? Registration[FaceIndex] : 0u;
}

// 📝 The fan triangles one face amounts to. `50` §2 ① accepts n-gons, so this is the corner count less two and
//    never the constant one — a partition budgeted as though every face were a triangle overruns by the amount
//    the artist's quads and n-gons exceed it, which on a subdivided surface is the whole budget again.
std::uint32_t TrianglesOfFace(const TopologyStructure& Imported, std::uint32_t FaceIndex)
{
    const std::uint32_t Corners = Imported.FaceCornerCount(FaceIndex);

    return Corners > 2u ? Corners - 2u : 0u;
}

/// 🧩 One face's own orientation, by Newell's summation over its corner run.
/// note  📐 Newell rather than a cross product of the first three corners, because an n-gon's first three corners
///        may be collinear or may sit on a locally concave part of an otherwise well-behaved face. Newell reads
///        every corner and produces the area-weighted orientation of the whole polygon, which is what the cone is
///        supposed to enclose.
/// note  📝 Accumulated at 64 bits and narrowed only at the end. The positions are `mm` in document space and
///        `02` §3.2 keeps them at 64 bits precisely because differencing them at 32 bits is where a distant
///        owner's geometry turns to noise.
SurfaceDirection OrientationOfFace(const TopologyStructure& Imported, std::uint32_t FaceIndex, bool& OrientationDerived)
{
    const std::vector<DocumentPosition>& Positions   = Imported.Positions();
    const std::uint32_t                  FirstCorner = Imported.FaceFirstCorner(FaceIndex);
    const std::uint32_t                  CornerRun   = Imported.FaceCornerCount(FaceIndex);

    double SummedX = 0.0;
    double SummedY = 0.0;
    double SummedZ = 0.0;

    for (std::uint32_t Step = 0u; Step < CornerRun; ++Step)
    {
        const std::uint32_t ThisCorner = FirstCorner + Step;
        const std::uint32_t NextCorner = FirstCorner + (Step + 1u) % CornerRun;

        const DocumentPosition& Here  = Positions[Imported.CornerVertex(ThisCorner)];
        const DocumentPosition& There = Positions[Imported.CornerVertex(NextCorner)];

        SummedX += (Here.PositionY - There.PositionY) * (Here.PositionZ + There.PositionZ);
        SummedY += (Here.PositionZ - There.PositionZ) * (Here.PositionX + There.PositionX);
        SummedZ += (Here.PositionX - There.PositionX) * (Here.PositionY + There.PositionY);
    }

    const double Magnitude = std::sqrt(SummedX * SummedX + SummedY * SummedY + SummedZ * SummedZ);

    SurfaceDirection Oriented;

    // ⚠️ A face of no area has no orientation, and normalising one produces three quiet infinities that
    //    propagate into the cone axis and reject the whole partition from every direction at once. The
    //    conditioning registers such a face as `ZeroExtentFace` and the growth steps over it; this guard is what
    //    covers a face that is registered under nothing and still sums to nothing.
    OrientationDerived = Magnitude > 0.0;

    if (!OrientationDerived)
        return Oriented;

    Oriented.DirectionX = static_cast<float>(SummedX / Magnitude);
    Oriented.DirectionY = static_cast<float>(SummedY / Magnitude);
    Oriented.DirectionZ = static_cast<float>(SummedZ / Magnitude);

    return Oriented;
}

// 📝 The extent one face contributes, folded into a running one. Both corners move outward and neither is
//    rounded in, per `38` §6 — the conditioning already rounded each face outward and taking the extremes of
//    outward extents keeps the result outward.
void AcceptExtent(ConditionedExtent& Running, const ConditionedExtent& Incoming, bool FirstAdmission)
{
    if (FirstAdmission)
    {
        Running = Incoming;
        return;
    }

    Running.Minimum.PositionX    = Incoming.Minimum.PositionX    < Running.Minimum.PositionX    ? Incoming.Minimum.PositionX    : Running.Minimum.PositionX;
    Running.Minimum.PositionY    = Incoming.Minimum.PositionY    < Running.Minimum.PositionY    ? Incoming.Minimum.PositionY    : Running.Minimum.PositionY;
    Running.Minimum.PositionZ    = Incoming.Minimum.PositionZ    < Running.Minimum.PositionZ    ? Incoming.Minimum.PositionZ    : Running.Minimum.PositionZ;
    Running.Maximum.PositionX = Incoming.Maximum.PositionX > Running.Maximum.PositionX ? Incoming.Maximum.PositionX : Running.Maximum.PositionX;
    Running.Maximum.PositionY = Incoming.Maximum.PositionY > Running.Maximum.PositionY ? Incoming.Maximum.PositionY : Running.Maximum.PositionY;
    Running.Maximum.PositionZ = Incoming.Maximum.PositionZ > Running.Maximum.PositionZ ? Incoming.Maximum.PositionZ : Running.Maximum.PositionZ;
}

/// 🧩 Closes the cone over the orientations one partition accumulated.
/// note  📐 The axis is the normalised sum of the face orientations and the aperture is the least dot product any
///        one of them takes against it. The sum is unweighted, so a partition of a thousand tiny faces and one
///        large one is centred where the faces are rather than where the area is — which is the direction the
///        aperture then has to enclose, so the two agree.
/// note  🔴 An aperture at or below zero spans a hemisphere or more, and no direction exists from which every
///        face is back-facing. The cone is withheld rather than reported wide, because `16` §2 ① reads only the
///        two numbers and cannot tell a wide cone from a meaningless one.
OrientationCone CloseCone(double SummedX, double SummedY, double SummedZ, double Aperture, bool EveryFaceOriented)
{
    OrientationCone Closed;

    const double Magnitude = std::sqrt(SummedX * SummedX + SummedY * SummedY + SummedZ * SummedZ);

    if (!EveryFaceOriented || Magnitude <= 0.0 || Aperture <= 0.0)
        return Closed;

    Closed.Axis.DirectionX = static_cast<float>(SummedX / Magnitude);
    Closed.Axis.DirectionY = static_cast<float>(SummedY / Magnitude);
    Closed.Axis.DirectionZ = static_cast<float>(SummedZ / Magnitude);
    Closed.ApertureCosine  = static_cast<float>(Aperture);
    Closed.ConeDerived     = true;

    return Closed;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DERIVATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<DerivedPartitioning> DerivePartitioning(const TopologyStructure&    Imported,
                                                const TopologyConditioning& Conditioned)
{
    if (!Imported.Sealed())
    {
        return Deliver<DerivedPartitioning>::Refuse(
            { RefusalReason::HostDenied, "an unsealed topology may still be declared into" });
    }

    if (Conditioned.ConditionedRevision() != Imported.Revision())
    {
        return Deliver<DerivedPartitioning>::Refuse(
            { RefusalReason::ContentUnsupported, "the conditioning describes another revision of this topology" });
    }

    const std::uint32_t FaceLimit = Imported.FaceCount();

    if (FaceLimit == 0u)
    {
        return Deliver<DerivedPartitioning>::Refuse(
            { RefusalReason::ContentUnsupported, "a topology of no face partitions into nothing" });
    }

    const std::vector<ConditionedExtent>& FaceExtents = Conditioned.FaceExtents();

    if (static_cast<std::uint32_t>(FaceExtents.size()) != FaceLimit)
    {
        return Deliver<DerivedPartitioning>::Refuse(
            { RefusalReason::ContentUnsupported, "the conditioning carries an extent count the topology does not" });
    }

    DerivedPartitioning Derived;
    Derived.DescribedRevision = Imported.Revision();
    Derived.OrderedFaces.reserve(FaceLimit);

    // 📝 Two marks and not one. Accepted says the face belongs to a closed partition and is never revisited;
    //    Enqueued says it is standing on a growth front that may yet close before reaching it, and that mark is
    //    lifted at the close so the face seeds or joins the next partition instead of vanishing from the surface.
    std::vector<bool> Accepted(FaceLimit, false);
    std::vector<bool> Enqueued(FaceLimit, false);

    std::vector<std::uint32_t> Front;
    Front.reserve(FaceLimit);

    bool MinimumRecorded = false;

    for (std::uint32_t SeedFace = 0u; SeedFace < FaceLimit; ++SeedFace)
    {
        if (Accepted[SeedFace])
            continue;

        if (Conditioned.FaceRegistered(SeedFace, DegeneracySubject::ZeroExtentFace))
        {
            // 📝 Counted once, here, rather than at every adjacency that meets it. A face reached from four
            //    neighbours would otherwise be reported four times and `86` would read the exclusion as larger
            //    than the surface it happened on.
            if (!Accepted[SeedFace])
                ++Derived.Metrics.ExcludedFaceCount;

            Accepted[SeedFace] = true;
            continue;
        }

        if (static_cast<std::uint64_t>(Derived.Partitions.size()) + 1u >= static_cast<std::uint64_t>(AbsentPartition))
        {
            return Deliver<DerivedPartitioning>::Refuse(
                { RefusalReason::ExtentExhausted, "the partition count would reach the ordinal reserved for absence" });
        }

        const std::uint32_t SeedMaterial = MaterialOfFace(Imported, SeedFace);

        MicroSurfacePartition Growing;
        Growing.FirstFace       = static_cast<std::uint32_t>(Derived.OrderedFaces.size());
        Growing.MaterialIndex = SeedMaterial;

        double SummedX        = 0.0;
        double SummedY        = 0.0;
        double SummedZ        = 0.0;
        double MinimumAgreement = 0.0;

        bool AgreementRecorded = false;
        bool EveryFaceOriented = true;
        bool FirstAdmission    = true;

        std::vector<SurfaceDirection> Orientations;

        Front.clear();
        Front.push_back(SeedFace);
        Enqueued[SeedFace] = true;

        std::size_t FrontHead = 0u;

        while (FrontHead < Front.size() && Growing.TriangleCount < PartitionTriangleLimit)
        {
            const std::uint32_t AcceptedFace = Front[FrontHead];
            ++FrontHead;

            Accepted[AcceptedFace] = true;
            Derived.OrderedFaces.push_back(AcceptedFace);
            ++Growing.FaceCount;
            Growing.TriangleCount += TrianglesOfFace(Imported, AcceptedFace);

            AcceptExtent(Growing.Extent, FaceExtents[AcceptedFace], FirstAdmission);
            FirstAdmission = false;

            bool                   FaceOriented = false;
            const SurfaceDirection Oriented     = OrientationOfFace(Imported, AcceptedFace, FaceOriented);

            if (FaceOriented)
            {
                SummedX += static_cast<double>(Oriented.DirectionX);
                SummedY += static_cast<double>(Oriented.DirectionY);
                SummedZ += static_cast<double>(Oriented.DirectionZ);
                Orientations.push_back(Oriented);
            }
            else
            {
                EveryFaceOriented = false;
            }

            const std::uint32_t FirstCorner = Imported.FaceFirstCorner(AcceptedFace);
            const std::uint32_t CornerRun   = Imported.FaceCornerCount(AcceptedFace);

            for (std::uint32_t Step = 0u; Step < CornerRun; ++Step)
            {
                const Deliver<std::uint32_t> Y = Conditioned.AdjacentCorner(FirstCorner + Step);

                // 📝 🔴 A refusal is where the surface stops, not where the derivation failed. `38` refuses at a
                //    boundary edge and at a non-manifold one rather than choosing among several faces, so the
                //    front simply does not cross here and the count is what `86` reads the partitioning's
                //    fragmentation from.
                if (!Y.Resolved)
                {
                    ++Derived.Metrics.EdgeRefusalCount;
                    continue;
                }

                const std::uint32_t Adjacent = Imported.CornerFace(Y.Resolve());

                if (Adjacent >= FaceLimit || Accepted[Adjacent] || Enqueued[Adjacent])
                    continue;

                if (Conditioned.FaceRegistered(Adjacent, DegeneracySubject::ZeroExtentFace))
                    continue;

                // 🔴 Growth stops at an enrollment change. `42`'s resolution carries one material ordinal per
                //    partition, so a partition spanning two resolves to whichever was recorded first and shades
                //    half of its own pixels with the other material's reflectance.
                if (MaterialOfFace(Imported, Adjacent) != SeedMaterial)
                    continue;

                Front.push_back(Adjacent);
                Enqueued[Adjacent] = true;
            }
        }

        // 📝 The front is lifted rather than discarded. Everything still standing on it was reachable and is not
        //    yet accepted, and leaving the mark down is how a partition that closed at its ceiling takes a ring
        //    of its own neighbours out of the surface with it.
        for (std::size_t Current = FrontHead; Current < Front.size(); ++Current)
            Enqueued[Front[Current]] = false;

        // 📐 Measured against the unnormalised sum and divided once at the end, rather than normalising the axis
        //    and then taking a dot product per face. One square root per partition instead of one per face, and
        //    the ordering of the comparisons is unaffected because the divisor is positive and common to all.
        // ⚠️ Seeded from the first face and not from one, because the products compared here carry the sum's own
        //    magnitude. A seed of one is a cosine, and against a partition of more than one face every real
        //    product exceeds it — the aperture then closes to a cone narrower than the faces occupy and the cull
        //    rejects a partition the artist is looking straight at.
        for (const SurfaceDirection& Compared : Orientations)
        {
            const double Agreement = static_cast<double>(Compared.DirectionX) * SummedX
                                   + static_cast<double>(Compared.DirectionY) * SummedY
                                   + static_cast<double>(Compared.DirectionZ) * SummedZ;

            if (!AgreementRecorded || Agreement < MinimumAgreement)
            {
                MinimumAgreement    = Agreement;
                AgreementRecorded = true;
            }
        }

        const double AxisMagnitude = std::sqrt(SummedX * SummedX + SummedY * SummedY + SummedZ * SummedZ);
        const double Aperture      = AgreementRecorded && AxisMagnitude > 0.0
                                   ? MinimumAgreement / AxisMagnitude
                                   : 0.0;

        Growing.Orientation = CloseCone(SummedX, SummedY, SummedZ, Aperture, EveryFaceOriented);

        if (!Growing.Orientation.ConeDerived)
            ++Derived.Metrics.ConelessCount;

        if (Growing.TriangleCount < PartitionTriangleFloor)
            ++Derived.Metrics.ShortPartitionCount;

        if (!MinimumRecorded || Growing.TriangleCount < Derived.Metrics.MinimumTriangleCount)
        {
            Derived.Metrics.MinimumTriangleCount = Growing.TriangleCount;
            MinimumRecorded                      = true;
        }

        if (Growing.TriangleCount > Derived.Metrics.MaximumTriangleCount)
            Derived.Metrics.MaximumTriangleCount = Growing.TriangleCount;

        Derived.Partitions.push_back(Growing);
    }

    Derived.Metrics.PartitionCount = static_cast<std::uint32_t>(Derived.Partitions.size());

    if (Derived.Partitions.empty())
    {
        return Deliver<DerivedPartitioning>::Refuse(
            { RefusalReason::ContentUnsupported, "every face of the topology is registered as zero-extent" });
    }

    return Deliver<DerivedPartitioning>::Result(Derived);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               ADOPTION AND DECLARATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> PartitionStructure::Adopt(const DerivedPartitioning& Incoming)
{
    if (Incoming.Partitions.empty())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a partitioning of no partition stands for nothing" });

    CurrentPartitioning = Incoming;

    // 📝 The identities go with the adoption. They were registered against the partition ordinals of the partitioning
    //    being replaced, and retaining them would let `IdentityOf` hand out an identity naming a partition the
    //    new partitioning numbers differently — which resolves, and resolves to the wrong surface.
    Identities.clear();

    ++AdoptedRevision;
    PartitioningAdopted = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> PartitionStructure::Declare(PartitionResolutionIndex& Resolutions, OwnerIdentity Owner)
{
    if (!PartitioningAdopted)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no partitioning stands to declare" });

    if (!Owner.IdentityDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the owner identity names no slot" });

    Identities.clear();
    Identities.reserve(CurrentPartitioning.Partitions.size());

    for (const MicroSurfacePartition& Current : CurrentPartitioning.Partitions)
    {
        ResolvedPartition Resolving;
        Resolving.Owner        = Owner;
        Resolving.MaterialIndex = Current.MaterialIndex;
        Resolving.FirstFace       = Current.FirstFace;
        Resolving.FaceCount       = Current.FaceCount;

        const Deliver<PartitionIdentity> Registered = Resolutions.Declare(Resolving);

        if (!Registered.Resolved)
        {
            // 📝 The retained identities are dropped on a partial declaration. Half a partitioning declared is
            //    one where `IdentityOf` answers for the low ordinals and refuses for the high ones, and the
            //    caller reads that as a partitioning with a hole rather than as this refusal.
            Identities.clear();

            return Deliver<bool>::Refuse(Registered.Error);
        }

        Identities.push_back(Registered.Resolve());
    }

    return Deliver<bool>::Result(true);
}

void PartitionStructure::Reclaim()
{
    CurrentPartitioning = {};
    Identities.clear();

    PartitioningAdopted = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE READS
//------------------------------------------------------------------------------------------------------------------------

const DerivedPartitioning& PartitionStructure::Current() const
{
    return CurrentPartitioning;
}

Deliver<PartitionIdentity> PartitionStructure::IdentityOf(std::uint32_t PartitionIndex) const
{
    if (PartitionIndex >= static_cast<std::uint32_t>(CurrentPartitioning.Partitions.size()))
        return Deliver<PartitionIdentity>::Refuse({ RefusalReason::ContentUnsupported, "no such partition" });

    if (Identities.size() != CurrentPartitioning.Partitions.size())
    {
        return Deliver<PartitionIdentity>::Refuse(
            { RefusalReason::IdentityStale, "nothing has been declared since the partitioning was adopted" });
    }

    return Deliver<PartitionIdentity>::Result(Identities[PartitionIndex]);
}

bool PartitionStructure::PartitioningCurrent() const
{
    return PartitioningAdopted;
}

std::uint64_t PartitionStructure::Revision() const
{
    return AdoptedRevision;
}

std::uint64_t PartitionStructure::DescribedRevision() const
{
    return CurrentPartitioning.DescribedRevision;
}

std::uint32_t PartitionStructure::PartitionCount() const
{
    return static_cast<std::uint32_t>(CurrentPartitioning.Partitions.size());
}

}   // namespace Slate
