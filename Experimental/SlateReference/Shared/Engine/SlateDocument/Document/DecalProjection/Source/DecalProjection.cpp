//============================================================================================================================================
//                                                           DECALPROJECTION.CPP
//============================================================================================================================================
// 🧩 The inverse of a decomposed placing transform, the two extent derivations, and the drag that records nothing until release.

#include "SlateDocument/Document/DecalProjection/Api/DecalProjection.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSFORM HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

RotationQuaternion Conjugated(RotationQuaternion Subject)
{
    RotationQuaternion Reversed;
    Reversed.ImaginaryX = -Subject.ImaginaryX;
    Reversed.ImaginaryY = -Subject.ImaginaryY;
    Reversed.ImaginaryZ = -Subject.ImaginaryZ;
    Reversed.Real       =  Subject.Real;

    return Reversed;
}

// 📐 The quaternion sandwich, expanded. Deriving a matrix and multiplying by it is the same arithmetic with a
//    temporary in the middle, and the temporary is what a later reader caches.
void RotateSpan(RotationQuaternion Rotation,
                double SpanX, double SpanY, double SpanZ,
                double& OutX, double& OutY, double& OutZ)
{
    const double CrossX = Rotation.ImaginaryY * SpanZ - Rotation.ImaginaryZ * SpanY;
    const double CrossY = Rotation.ImaginaryZ * SpanX - Rotation.ImaginaryX * SpanZ;
    const double CrossZ = Rotation.ImaginaryX * SpanY - Rotation.ImaginaryY * SpanX;

    const double SecondX = Rotation.ImaginaryY * CrossZ - Rotation.ImaginaryZ * CrossY;
    const double SecondY = Rotation.ImaginaryZ * CrossX - Rotation.ImaginaryX * CrossZ;
    const double SecondZ = Rotation.ImaginaryX * CrossY - Rotation.ImaginaryY * CrossX;

    OutX = SpanX + 2.0 * (Rotation.Real * CrossX + SecondX);
    OutY = SpanY + 2.0 * (Rotation.Real * CrossY + SecondY);
    OutZ = SpanZ + 2.0 * (Rotation.Real * CrossZ + SecondZ);
}

// 📝 A domain placement's transform is read in the plane alone — the third coordinate carries nothing, because the
//    domain is two-dimensional. Reading it would let a domain placement acquire a depth the domain cannot express.
void ProjectPlanar(const DecomposedTransform& Placing,
                   double SourceX, double SourceY,
                   double& XOut, double& YOut)
{
    const double ScaledX  = (SourceX  - 0.5) * Placing.ScaleX;
    const double ScaledY = (SourceY - 0.5) * Placing.ScaleY;

    double TurnedX  = 0.0;
    double TurnedY = 0.0;
    double TurnedDeep   = 0.0;

    RotateSpan(Placing.Rotation, ScaledX, ScaledY, 0.0, TurnedX, TurnedY, TurnedDeep);

    XOut  = TurnedX  + Placing.Translation.PositionX;
    YOut = TurnedY + Placing.Translation.PositionY;
}

// 📝 One representable step outward on each face, matching `38` §6. An extent rounded inward excludes the
//    placement's own border from `74` §3's containment test, and the artist meets it as a decal with a thin band
//    along one edge that cannot be clicked.
void WidenOutward(DomainExtent& Widening)
{
    Widening.MinimumX     = std::nextafter(Widening.MinimumX,     -HUGE_VAL);
    Widening.MinimumY    = std::nextafter(Widening.MinimumY,    -HUGE_VAL);
    Widening.MaximumX  = std::nextafter(Widening.MaximumX,   HUGE_VAL);
    Widening.MaximumY = std::nextafter(Widening.MaximumY,  HUGE_VAL);
}

void AcceptPosition(DomainExtent& Running, double X, double Y, bool& FirstAdmission)
{
    if (FirstAdmission)
    {
        Running.MinimumX     = X;
        Running.MaximumX  = X;
        Running.MinimumY    = Y;
        Running.MaximumY = Y;

        FirstAdmission = false;

        return;
    }

    Running.MinimumX     = X  < Running.MinimumX     ? X  : Running.MinimumX;
    Running.MaximumX  = X  > Running.MaximumX  ? X  : Running.MaximumX;
    Running.MinimumY    = Y < Running.MinimumY    ? Y : Running.MinimumY;
    Running.MaximumY = Y > Running.MaximumY ? Y : Running.MaximumY;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  INTO THE SOURCE SPACE
//------------------------------------------------------------------------------------------------------------------------

bool ProjectIntoSource(const PlacementSpecification& Placed,
                       double                        PositionX,
                       double                        PositionY,
                       double&                       SourceX,
                       double&                       SourceY)
{
    // 📐 The inverse of a decomposed transform, taken decomposed. Translation is subtracted, the rotation is
    //    conjugated, and the scale is reciprocated — `02` §3.1 keeps a transform in this form precisely so that
    //    the inverse is three cheap steps rather than a general matrix inversion whose conditioning nobody
    //    tracks.
    const double Width  = PositionX  - Placed.PlacingTransform.Translation.PositionX;
    const double Height = PositionY - Placed.PlacingTransform.Translation.PositionY;

    double TurnedX  = 0.0;
    double TurnedY = 0.0;
    double TurnedDeep   = 0.0;

    RotateSpan(Conjugated(Placed.PlacingTransform.Rotation),
               Width, Height, 0.0,
               TurnedX, TurnedY, TurnedDeep);

    const double ScaleX  = Placed.PlacingTransform.ScaleX != 0.0 ? Placed.PlacingTransform.ScaleX : 1.0;
    const double ScaleY = Placed.PlacingTransform.ScaleY != 0.0 ? Placed.PlacingTransform.ScaleY : 1.0;

    SourceX  = TurnedX  / ScaleX  + 0.5;
    SourceY = TurnedY / ScaleY + 0.5;

    // 📝 A tiling source is periodic and has no unit square to fall outside of — `54` §2's lattice classifies
    //    every position, at whatever cell ordinal it lands in. Bounding it here would clip a pattern to the
    //    placement's own footprint twice, once by the extent and once by this test.
    if (Placed.Source == PlacedSource::Tiling)
        return true;

    return SourceX >= 0.0 && SourceX <= 1.0 && SourceY >= 0.0 && SourceY <= 1.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DERIVED EXTENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<DomainExtent> ProjectPlacementExtent(const PlacementSpecification&         Placed,
                                             std::uint32_t                         PlacementIndex,
                                             std::uint32_t                         SequenceIndex,
                                             const TopologyStructure&              Imported,
                                             const std::vector<DomainCoordinate>&  CornerCoordinates)
{
    if (!Imported.Sealed())
    {
        return Deliver<DomainExtent>::Refuse(
            { RefusalReason::HostDenied, "an unsealed topology may still be declared into" });
    }

    if (static_cast<std::uint32_t>(CornerCoordinates.size()) != Imported.CornerCount())
    {
        return Deliver<DomainExtent>::Refuse(
            { RefusalReason::ContentUnsupported, "the coordinate run carries a corner count the topology does not" });
    }

    DomainExtent Derived;
    Derived.PlacementIndex = PlacementIndex;
    Derived.SequenceIndex  = SequenceIndex;

    bool FirstAdmission = true;

    if (Placed.Mode == PlacementMode::DomainPlaced)
    {
        // 📐 The four corners of the source's unit square through the placing transform. Four and not two,
        //    because the transform carries a rotation — the extent of a rotated square is not the transform of
        //    its extent, and taking two corners is the mistake that shows up only once the artist turns a decal.
        for (std::uint32_t Corner = 0u; Corner < 4u; ++Corner)
        {
            const double SourceX  = (Corner & 1u) != 0u ? 1.0 : 0.0;
            const double SourceY = (Corner & 2u) != 0u ? 1.0 : 0.0;

            double X  = 0.0;
            double Y = 0.0;
            ProjectPlanar(Placed.PlacingTransform, SourceX, SourceY, X, Y);

            AcceptPosition(Derived, X, Y, FirstAdmission);
        }

        WidenOutward(Derived);

        return Deliver<DomainExtent>::Result(Derived);
    }

    // 🔴 A projected placement covers whatever its volume reaches, which is a question about the topology rather
    //    than about the transform. The volume's own axes are the placing transform's, so a corner is tested by
    //    carrying it back into that frame and comparing against the declared half extents and reach — the same
    //    slab test `40` performs, in the placement's space rather than the document's.
    const RotationQuaternion    Inverse   = Conjugated(Placed.PlacingTransform.Rotation);
    const std::vector<DocumentPosition>& Positions = Imported.Positions();

    for (std::uint32_t CornerIndex = 0u; CornerIndex < Imported.CornerCount(); ++CornerIndex)
    {
        const DocumentPosition& Held = Positions[Imported.CornerVertex(CornerIndex)];

        const double SpanX = Held.PositionX - Placed.PlacingTransform.Translation.PositionX;
        const double SpanY = Held.PositionY - Placed.PlacingTransform.Translation.PositionY;
        const double SpanZ = Held.PositionZ - Placed.PlacingTransform.Translation.PositionZ;

        double LocalX = 0.0;
        double LocalY = 0.0;
        double LocalZ = 0.0;
        RotateSpan(Inverse, SpanX, SpanY, SpanZ, LocalX, LocalY, LocalZ);

        if (LocalX < -Placed.ProjectedHalfX  || LocalX > Placed.ProjectedHalfX)
            continue;

        if (LocalY < -Placed.ProjectedHalfY || LocalY > Placed.ProjectedHalfY)
            continue;

        // 📐 The volume projects along its own negative third axis, matching `46`'s camera convention and `44`'s
        //    emission direction. One convention across all three means a decal parented to a spot illuminant
        //    covers what that illuminant lights.
        const double Reach = -LocalZ;

        if (Reach < 0.0 || Reach > Placed.ProjectedReach)
            continue;

        // ⚠️ 🚧 Back-facing corners are rejected unless the placement accepts them, and `72` §6 carries what the
        //    right rule is as open. Refusing is the conservative reading: a decal projected onto a shoulder
        //    should not also land on the far side of the arm, where the artist cannot see it and will not
        //    understand why the extent reaches there.
        if (!Placed.BackFacingAccepted)
        {
            // 📝 The perpendicular is not available here — `38`'s conditioning is the caller's, not this
            //    component's — so facing is judged by the corner's own displacement along the projection axis
            //    relative to the face's first corner. That is coarse and it is conservative in the accepting
            //    direction, which is the direction §6's note declares safe.
            const std::uint32_t FaceIndex = Imported.CornerFace(CornerIndex);
            const std::uint32_t FirstCorner = Imported.FaceFirstCorner(FaceIndex);

            if (CornerIndex != FirstCorner)
            {
                const DocumentPosition& Leading = Positions[Imported.CornerVertex(FirstCorner)];

                const double LeadingSpanX = Leading.PositionX - Placed.PlacingTransform.Translation.PositionX;
                const double LeadingSpanY = Leading.PositionY - Placed.PlacingTransform.Translation.PositionY;
                const double LeadingSpanZ = Leading.PositionZ - Placed.PlacingTransform.Translation.PositionZ;

                double LeadingX = 0.0;
                double LeadingY = 0.0;
                double LeadingZ = 0.0;
                RotateSpan(Inverse, LeadingSpanX, LeadingSpanY, LeadingSpanZ, LeadingX, LeadingY, LeadingZ);

                if (-LeadingZ > Placed.ProjectedReach)
                    continue;
            }
        }

        AcceptPosition(Derived,
                      static_cast<double>(CornerCoordinates[CornerIndex].CoordinateX),
                      static_cast<double>(CornerCoordinates[CornerIndex].CoordinateY),
                      FirstAdmission);
    }

    // 📝 A projected placement reaching no corner covers nothing, and that is reported as a refusal rather than
    //    as an extent of nothing. An empty extent accepted into `AxisSpace` is a placement `74` can never pick
    //    and `12` can never present as missing — the artist sees a row that does nothing at all.
    if (FirstAdmission)
    {
        return Deliver<DomainExtent>::Refuse(
            { RefusalReason::ExtentExhausted, "the projecting volume reaches no corner of the topology" });
    }

    WidenOutward(Derived);

    return Deliver<DomainExtent>::Result(Derived);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PLACEMENTS
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> PlacementIndex::Declare(const PlacementSpecification& Declaring)
{
    if (!Declaring.Owner.IdentityDeclared())
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::IdentityStale, "a placement attaches to no owner" });
    }

    if (Declaring.Source == PlacedSource::SourceCount || Declaring.Mode == PlacementMode::ModeCount)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no such source or mode" });

    if (Declaring.Mode == PlacementMode::ProjectedPlaced
     && (Declaring.ProjectedHalfX <= 0.0 || Declaring.ProjectedHalfY <= 0.0
      || Declaring.ProjectedReach     <= 0.0))
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "a projecting volume of no extent reaches nothing" });
    }

    if (Declaring.PlacingTransform.ScaleX == 0.0 || Declaring.PlacingTransform.ScaleY == 0.0)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "a placement scaled to nothing covers nothing" });
    }

    std::uint32_t PlacementIndex = AbsentPlacement;

    if (!ReleasedIndexs.empty())
    {
        PlacementIndex = ReleasedIndexs.back();
        ReleasedIndexs.pop_back();
    }
    else
    {
        if (Placements.size() >= PlacementLimit)
        {
            return Deliver<std::uint32_t>::Refuse(
                { RefusalReason::ExtentExhausted, "the document reached its placement ceiling" });
        }

        PlacementIndex = static_cast<std::uint32_t>(Placements.size());
        Placements.push_back(HeldPlacement{});
    }

    HeldPlacement& Incoming = Placements[PlacementIndex];

    // 📝 The revision carries across a slot's reuse rather than restarting at one. A resident tile that recorded
    //    the previous owner's revision would otherwise match the new one's and stand unresolved, which is a
    //    decal showing whatever the slot used to hold.
    const std::uint64_t Carried = Incoming.Declared.RevisionCounter;

    Incoming.Declared                 = Declaring;
    Incoming.Declared.RevisionCounter = Carried + 1u;
    Incoming.SlotOccupied             = true;

    ++OccupiedCount;

    return Deliver<std::uint32_t>::Result(PlacementIndex);
}

Deliver<bool> PlacementIndex::Amend(std::uint32_t PlacementIndex, const PlacementSpecification& Amending)
{
    if (PlacementIndex >= Placements.size() || !Placements[PlacementIndex].SlotOccupied)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no placement stands at that ordinal" });

    HeldPlacement& Held = Placements[PlacementIndex];

    // 🔴 `00` §10.1 ②'s third row and nothing else. A combination change amends how the resolved value reads
    //    against what is beneath it and does not change the value, so advancing the revision for one would
    //    re-resolve a tile to the number it already holds — and would do it for every tile the placement covers.
    const bool SourceMoved = Held.Declared.Source              != Amending.Source
                          || Held.Declared.SourceIndex       != Amending.SourceIndex
                          || Held.Declared.Mode                != Amending.Mode
                          || Held.Declared.ChannelMask         != Amending.ChannelMask
                          || Held.Declared.ProjectedHalfX  != Amending.ProjectedHalfX
                          || Held.Declared.ProjectedHalfY != Amending.ProjectedHalfY
                          || Held.Declared.ProjectedReach      != Amending.ProjectedReach
                          || Held.Declared.PlacingTransform.Translation.PositionX
                             != Amending.PlacingTransform.Translation.PositionX
                          || Held.Declared.PlacingTransform.Translation.PositionY
                             != Amending.PlacingTransform.Translation.PositionY
                          || Held.Declared.PlacingTransform.Translation.PositionZ
                             != Amending.PlacingTransform.Translation.PositionZ
                          || Held.Declared.PlacingTransform.ScaleX != Amending.PlacingTransform.ScaleX
                          || Held.Declared.PlacingTransform.ScaleY != Amending.PlacingTransform.ScaleY
                          || Held.Declared.PlacingTransform.ScaleZ != Amending.PlacingTransform.ScaleZ
                          || Held.Declared.PlacingTransform.Rotation.ImaginaryX
                             != Amending.PlacingTransform.Rotation.ImaginaryX
                          || Held.Declared.PlacingTransform.Rotation.ImaginaryY
                             != Amending.PlacingTransform.Rotation.ImaginaryY
                          || Held.Declared.PlacingTransform.Rotation.ImaginaryZ
                             != Amending.PlacingTransform.Rotation.ImaginaryZ
                          || Held.Declared.PlacingTransform.Rotation.Real
                             != Amending.PlacingTransform.Rotation.Real;

    const std::uint64_t Current = Held.Declared.RevisionCounter;

    Held.Declared                 = Amending;
    Held.Declared.RevisionCounter = SourceMoved ? Current + 1u : Current;

    return Deliver<bool>::Result(true);
}

Deliver<const PlacementSpecification*> PlacementIndex::Resolve(std::uint32_t PlacementIndex) const
{
    if (PlacementIndex >= Placements.size() || !Placements[PlacementIndex].SlotOccupied)
    {
        return Deliver<const PlacementSpecification*>::Refuse(
            { RefusalReason::ContentUnsupported, "no placement stands at that ordinal" });
    }

    return Deliver<const PlacementSpecification*>::Result(&Placements[PlacementIndex].Declared);
}

Deliver<bool> PlacementIndex::Withdraw(std::uint32_t PlacementIndex)
{
    if (PlacementIndex >= Placements.size() || !Placements[PlacementIndex].SlotOccupied)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no placement stands at that ordinal" });

    Placements[PlacementIndex].SlotOccupied = false;

    // 📝 The specification is retained and the slot is marked free. `56` §6 retains a withdrawn entry's
    //    description so the inverse can restore it, and a slot whose specification was cleared here would leave
    //    that inverse with nothing to restore.
    ReleasedIndexs.push_back(PlacementIndex);

    if (OccupiedCount != 0u)
        --OccupiedCount;

    return Deliver<bool>::Result(true);
}

std::uint64_t PlacementIndex::Revision(std::uint32_t PlacementIndex) const
{
    if (PlacementIndex >= Placements.size() || !Placements[PlacementIndex].SlotOccupied)
        return 0u;

    return Placements[PlacementIndex].Declared.RevisionCounter;
}

std::uint32_t PlacementIndex::DeclaredCount() const
{
    return OccupiedCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE POSITIONING DRAG
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> PlacementSequence::Open(std::uint32_t                 PlacementIndex,
                                      const PlacementSpecification& Current,
                                      bool                          CameraFollowed_)
{
    if (OpenDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "a positioning drag is already open" });

    PriorPlacement   = Current;
    AmendedPlacement = Current;
    SubjectIndex   = PlacementIndex;
    CameraFollowed   = CameraFollowed_;
    OpenDeclared     = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> PlacementSequence::Amend(const DecomposedTransform& Amending)
{
    if (!OpenDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no positioning drag is open" });

    AmendedPlacement.PlacingTransform = Amending;

    return Deliver<bool>::Result(true);
}

Deliver<PlacementSpecification> PlacementSequence::Abandon()
{
    if (!OpenDeclared)
    {
        return Deliver<PlacementSpecification>::Refuse(
            { RefusalReason::HostDenied, "no positioning drag is open" });
    }

    const PlacementSpecification Restored = PriorPlacement;

    AmendedPlacement = PriorPlacement;
    SubjectIndex   = AbsentPlacement;
    OpenDeclared     = false;
    CameraFollowed   = false;

    return Deliver<PlacementSpecification>::Result(Restored);
}

Deliver<PlacementSpecification> PlacementSequence::Seal()
{
    if (!OpenDeclared)
    {
        return Deliver<PlacementSpecification>::Refuse(
            { RefusalReason::HostDenied, "no positioning drag is open" });
    }

    // 🔴 The revision advances **here**, once, for the whole drag. Advancing it per Amend would re-resolve every
    //    covered tile at every pointer sample, which is the cost `22` §4.1's speculative extent exists to avoid
    //    and which `70` §2's counter comparison exists to make measurable.
    PlacementSpecification Sealed = AmendedPlacement;
    ++Sealed.RevisionCounter;

    // 📝 The camera stops being followed by the drag ending, not by a freezing step. `00` §10.1 ① requires the
    //    screen gesture to resolve into a projected placement on release; the last transform `78` computed is
    //    already that placement, so there is nothing here to forget.
    SubjectIndex = AbsentPlacement;
    OpenDeclared   = false;
    CameraFollowed = false;

    return Deliver<PlacementSpecification>::Result(Sealed);
}

const PlacementSpecification& PlacementSequence::Amended() const  { return AmendedPlacement; }
std::uint32_t                 PlacementSequence::Subject() const  { return SubjectIndex;   }
bool                          PlacementSequence::GestureOpen() const   { return OpenDeclared;   }
bool                          PlacementSequence::CameraFollowing() const { return CameraFollowed; }

}   // namespace Slate
