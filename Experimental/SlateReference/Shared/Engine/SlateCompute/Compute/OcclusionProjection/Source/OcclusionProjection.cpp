//============================================================================================================================================
//                                                         OCCLUSIONPROJECTION.CPP
//============================================================================================================================================
// 🧩 Six faces about a position, a subdivision snapped against shimmer, the packing that truncates rather than drops, and the rebuild that almost never runs.

#include "SlateCompute/Compute/OcclusionProjection/Api/OcclusionProjection.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SIX FACINGS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The recording's own name, spelled once. The schedule orders by it and `86` reports by it, and two spellings
//    of one name are two recordings as far as the ordering is concerned.
const char* const OcclusionRecordingIdentity = "60-OcclusionProjection";

// 🔴 `08` §5's substitution, declared rather than branched. Where a device declines the projection extent, every
//    registered illuminant falls back to one face at the coarsest extent the device did admit — coarser shadows
//    everywhere and correct everywhere, rather than one illuminant silently casting none.
const char* const OcclusionSubstitution =
    "one face per illuminant at the coarsest accepted extent; the subdivision withdrawn";

constexpr double RootHalf = 0.7071067811865476;   // [-] - sin and cos of a quarter turn's half-angle

// 📐 The six facings a point illuminant projects along, as rotations of the camera convention. `46` §3 makes a
//    camera look down its own negative third axis, so a facing is the rotation that carries that axis onto it —
//    and reusing that convention is what lets `46`'s own derivation build these projections rather than a second
//    reversed-depth derivation written here.
RotationQuaternion FacingOf(std::uint32_t FaceIndex)
{
    RotationQuaternion Facing;

    switch (FaceIndex)
    {
        case 0u:   // -Z, the convention's own facing
            break;

        case 1u:   // +Z
            Facing.ImaginaryY = 1.0;
            Facing.Real       = 0.0;
            break;

        case 2u:   // -X
            Facing.ImaginaryY = RootHalf;
            Facing.Real       = RootHalf;
            break;

        case 3u:   // +X
            Facing.ImaginaryY = -RootHalf;
            Facing.Real       =  RootHalf;
            break;

        case 4u:   // -Y
            Facing.ImaginaryX = -RootHalf;
            Facing.Real       =  RootHalf;
            break;

        default:   // +Y
            Facing.ImaginaryX = RootHalf;
            Facing.Real       = RootHalf;
            break;
    }

    return Facing;
}

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

// 📐 The bounding sphere of one slice of a perspective camera's own volume, taken from its eight corners rather
//    than from a closed form. The closed form is two lines and has a case where the sphere's centre leaves the
//    slice entirely; the corners are eight products and have none, and this runs once per subdivision per
//    material camera move rather than once per pixel.
void SliceBounding(const CameraSpecification& Camera,
                   double                     Nearest,
                   double                     Furthest,
                   DocumentPosition&          Centre,
                   double&                    Radius)
{
    const double HalfY = Camera.ExtentParameter * 0.5 * Pi / 180.0;
    const double TangentY = std::tan(HalfY);
    const double TangentX  = TangentY * Camera.SensorProportion;

    double AccumulatedX = 0.0;
    double AccumulatedY = 0.0;
    double AccumulatedZ = 0.0;

    double CornerX[8] = {};
    double CornerY[8] = {};
    double CornerZ[8] = {};

    const double Distances[2] = { Nearest, Furthest };
    const double Signums[2]   = { -1.0, 1.0 };

    std::uint32_t Written = 0u;

    for (std::uint32_t Depth = 0u; Depth < 2u; ++Depth)
    {
        for (std::uint32_t Corner = 0u; Corner < 4u; ++Corner)
        {
            const double Distance = Distances[Depth];

            const double ViewX = Signums[Corner & 1u]        * TangentX  * Distance;
            const double ViewY = Signums[(Corner >> 1) & 1u] * TangentY * Distance;

            double TurnedX = 0.0;
            double TurnedY = 0.0;
            double TurnedZ = 0.0;
            RotateSpan(Camera.Placement.Rotation, ViewX, ViewY, -Distance, TurnedX, TurnedY, TurnedZ);

            CornerX[Written] = Camera.Placement.Translation.PositionX + TurnedX;
            CornerY[Written] = Camera.Placement.Translation.PositionY + TurnedY;
            CornerZ[Written] = Camera.Placement.Translation.PositionZ + TurnedZ;

            AccumulatedX += CornerX[Written];
            AccumulatedY += CornerY[Written];
            AccumulatedZ += CornerZ[Written];

            ++Written;
        }
    }

    Centre.PositionX = AccumulatedX / 8.0;
    Centre.PositionY = AccumulatedY / 8.0;
    Centre.PositionZ = AccumulatedZ / 8.0;

    Radius = 0.0;

    for (std::uint32_t Corner = 0u; Corner < 8u; ++Corner)
    {
        const double SpanX = CornerX[Corner] - Centre.PositionX;
        const double SpanY = CornerY[Corner] - Centre.PositionY;
        const double SpanZ = CornerZ[Corner] - Centre.PositionZ;

        const double Distance = std::sqrt(SpanX * SpanX + SpanY * SpanY + SpanZ * SpanZ);

        Radius = Distance > Radius ? Distance : Radius;
    }
}

// 📐 🔴 The slice's radius and its centre are both snapped to whole texels of the projection. Without the snap
//    the projection slides continuously as the camera moves and every recorded coordinate lands between two texels
//    of where it landed last rotation — which the artist meets as every shadow edge crawling while they orbit,
//    and which no amount of filtering removes because the source itself is moving.
void SnapSlice(DocumentPosition& Centre, double& Radius, RotationQuaternion Facing, std::uint32_t ExtentTexels)
{
    // 📝 The radius is snapped first and upward, so a slice whose radius jitters by a hair does not change the
    //    texel size the centre is then snapped against. Snapping the centre against a moving texel size is the
    //    same crawl one step removed.
    const double Quantum = 1.0;

    Radius = std::ceil(Radius / Quantum) * Quantum;

    const double TexelSpan = (2.0 * Radius) / static_cast<double>(ExtentTexels);

    if (TexelSpan <= 0.0)
        return;

    // 📝 Snapped in the projection's **own** frame and carried back, because the texel grid is the projection's
    //    and not the document's. Snapping in document space quantises along the wrong three axes and leaves the
    //    crawl untouched at every orientation but three.
    RotationQuaternion Inverse = Facing;
    Inverse.ImaginaryX = -Facing.ImaginaryX;
    Inverse.ImaginaryY = -Facing.ImaginaryY;
    Inverse.ImaginaryZ = -Facing.ImaginaryZ;

    double LocalX = 0.0;
    double LocalY = 0.0;
    double LocalZ = 0.0;
    RotateSpan(Inverse, Centre.PositionX, Centre.PositionY, Centre.PositionZ, LocalX, LocalY, LocalZ);

    LocalX = std::floor(LocalX / TexelSpan) * TexelSpan;
    LocalY = std::floor(LocalY / TexelSpan) * TexelSpan;

    double SnappedX = 0.0;
    double SnappedY = 0.0;
    double SnappedZ = 0.0;
    RotateSpan(Facing, LocalX, LocalY, LocalZ, SnappedX, SnappedY, SnappedZ);

    Centre.PositionX = SnappedX;
    Centre.PositionY = SnappedY;
    Centre.PositionZ = SnappedZ;
}

bool ExtentsOverlap(const PartitionExtent& Left, DocumentPosition Position, double Reach)
{
    return Left.Maximum.PositionX >= Position.PositionX - Reach
        && Left.Minimum.PositionX    <= Position.PositionX + Reach
        && Left.Maximum.PositionY >= Position.PositionY - Reach
        && Left.Minimum.PositionY    <= Position.PositionY + Reach
        && Left.Maximum.PositionZ >= Position.PositionZ - Reach
        && Left.Minimum.PositionZ    <= Position.PositionZ + Reach;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PACKED INDEX
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OcclusionIndex::Derive(const IlluminantIndex& Reaching, const IlluminantPopulation& Illuminants)
{
    const std::uint32_t Spanned = Reaching.SpannedCount();

    if (Spanned == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the reaching index spans no partition" });

    Packed.assign(Spanned, PackedPartition{});
    TruncatedAccumulated = 0u;

    for (std::uint32_t PartitionIndex = 0u; PartitionIndex < Spanned; ++PartitionIndex)
    {
        PackedPartition& Packing = Packed[PartitionIndex];

        const std::uint32_t ReachingCount = Reaching.ReachingCount(PartitionIndex);

        // 📝 Walked in the reaching set's own order, which `44` guarantees is identity order. The packing is
        //    therefore identity-ordered too, so a pixel's fourth component carries the same illuminant on every
        //    rotation and on every machine — and `18` unpacks by position without a second ordering to consult.
        for (std::uint32_t ReachIndex = 0u; ReachIndex < ReachingCount; ++ReachIndex)
        {
            const Deliver<OwnerIdentity> Named = Reaching.Reaching(PartitionIndex, ReachIndex);

            if (!Named.Resolved)
                continue;

            const Deliver<IlluminantSpecification> Declared = Illuminants.Resolve(Named.Resolve());

            // 🔴 An illuminant not registered for occlusion occupies **no slot**. `44` §2 gives the artist the
            //    switch and `60` §3 declares the unenrolled illuminant integrated unattenuated; a slot spent on
            //    one that casts nothing is a slot the next illuminant that does cast something cannot have.
            if (!Declared.Resolved || !Declared.Resolve().OcclusionRegistered)
                continue;

            const std::uint32_t Slot = ProjectOcclusionSlot(Packing.OccupiedCount);

            if (Slot == SlateOcclusionSlotAbsent)
            {
                ++Packing.TruncatedCount;
                ++TruncatedAccumulated;
                continue;
            }

            Packing.Occupying[Slot] = Named.Resolve();
            ++Packing.OccupiedCount;
        }
    }

    return Deliver<bool>::Result(true);
}

Deliver<std::uint32_t> OcclusionIndex::SlotOf(std::uint32_t PartitionIndex, OwnerIdentity Illuminant) const
{
    if (PartitionIndex >= Packed.size())
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no such partition" });

    const PackedPartition& Packing = Packed[PartitionIndex];

    for (std::uint32_t Slot = 0u; Slot < Packing.OccupiedCount; ++Slot)
    {
        if (Packing.Occupying[Slot] == Illuminant)
            return Deliver<std::uint32_t>::Result(Slot);
    }

    // 🔴 The two refusals are different facts. Truncated means the illuminant reaches the partition and `18`
    //    must integrate it unattenuated; unreached means it contributes nothing there and needs no shadow, and
    //    conflating them makes every distant illuminant look like a truncation in `86`'s register.
    if (Packing.TruncatedCount != 0u)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "the packed word cannot carry it; integrate it unattenuated" });
    }

    return Deliver<std::uint32_t>::Refuse(
        { RefusalReason::ContentUnsupported, "the illuminant does not reach that partition" });
}

Deliver<OwnerIdentity> OcclusionIndex::IlluminantAt(std::uint32_t PartitionIndex, std::uint32_t Slot) const
{
    if (PartitionIndex >= Packed.size() || Slot >= Packed[PartitionIndex].OccupiedCount)
        return Deliver<OwnerIdentity>::Refuse({ RefusalReason::ExtentExhausted, "the slot carries nothing" });

    return Deliver<OwnerIdentity>::Result(Packed[PartitionIndex].Occupying[Slot]);
}

std::uint32_t OcclusionIndex::TruncatedCount(std::uint32_t PartitionIndex) const
{
    return PartitionIndex < Packed.size() ? Packed[PartitionIndex].TruncatedCount : 0u;
}

std::uint32_t OcclusionIndex::TruncatedTotal() const { return TruncatedAccumulated; }

std::uint32_t OcclusionIndex::SpannedCount() const
{
    return static_cast<std::uint32_t>(Packed.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE AMBIENT TERM
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> AmbientOcclusionSequence::Declare(const AmbientOcclusionSpecification& Declaring)
{
    if (Declaring.SampleRadius <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a hemisphere of no radius closes nothing" });

    if (Declaring.SampleCount == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a term of no sample resolves nothing" });

    // ⚠️ Rejected above two rather than accepted as a quality setting. `08` §2 claims `OcclusionSurface` at half
    //    extent and `Shared/`'s upsample reads exactly four taps against that claim; a third would need a
    //    different tap count, and the extent would then be declared in two places that disagree.
    if (Declaring.ExtentDivisor != 2u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "`08` §2 claims the target at half extent and nowhere else" });
    }

    Specification = Declaring;

    return Deliver<bool>::Result(true);
}

Deliver<bool> AmbientOcclusionSequence::Resolve(std::uint32_t  DisplayX,
                                                std::uint32_t  DisplayY,
                                                std::uint32_t& ResolvedX,
                                                std::uint32_t& ResolvedY) const
{
    if (DisplayX == 0u || DisplayY == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a display extent of nothing" });

    // 📐 Rounded up on both ordinates, matching `RenderSchedule`'s own fraction-of-display claim exactly. The two
    //    rounding the same way is what makes the target this component resolves into the target `08` claimed.
    ResolvedX  = (DisplayX  + Specification.ExtentDivisor - 1u) / Specification.ExtentDivisor;
    ResolvedY = (DisplayY + Specification.ExtentDivisor - 1u) / Specification.ExtentDivisor;

    return Deliver<bool>::Result(true);
}

const AmbientOcclusionSpecification& AmbientOcclusionSequence::Declared() const { return Specification; }

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OcclusionProjectionSpace::Contribute(RenderSchedule& Schedule) const
{
    DeclaredRecording Declared;
    Declared.Identity = OcclusionRecordingIdentity;

    // 🔴 Both targets from one recording. Both are resolved from one reconstruction of the same depth, so two
    //    recordings would reconstruct the position at every pixel twice to write two scalars — and the two
    //    reconstructions would then have to agree, which is `18` §1's rule stated one document early.
    Declared.Produces = { SharedTarget::OcclusionSurface, SharedTarget::DirectOcclusionSurface };
    Declared.Reads    = { SharedTarget::DepthSurface };
    Declared.Amends   = {};

    Declared.Command            = RecordingCommand::ComputeDispatch;
    Declared.CapabilityRequired = true;
    Declared.Substitution       = OcclusionSubstitution;
    Declared.DisplayReferred    = false;

    return Schedule.Contribute(Declared);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CAMERA
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OcclusionProjectionSpace::DeclareCamera(const CameraSpecification& Declaring)
{
    if (!Declaring.Clipping.IntervalValid())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the camera declares no clipping interval to subdivide" });
    }

    if (CameraDeclared)
    {
        const double SpanX = Declaring.Placement.Translation.PositionX - ReferenceCamera.Placement.Translation.PositionX;
        const double SpanY = Declaring.Placement.Translation.PositionY - ReferenceCamera.Placement.Translation.PositionY;
        const double SpanZ = Declaring.Placement.Translation.PositionZ - ReferenceCamera.Placement.Translation.PositionZ;

        const double Moved = std::sqrt(SpanX * SpanX + SpanY * SpanY + SpanZ * SpanZ);

        const bool RangeAmended = Declaring.Clipping.Nearest        != ReferenceCamera.Clipping.Nearest
                               || Declaring.Clipping.Furthest       != ReferenceCamera.Clipping.Furthest
                               || Declaring.ExtentParameter         != ReferenceCamera.ExtentParameter
                               || Declaring.SensorProportion        != ReferenceCamera.SensorProportion;

        const bool RotationAmended =
            Declaring.Placement.Rotation.ImaginaryX != ReferenceCamera.Placement.Rotation.ImaginaryX ||
            Declaring.Placement.Rotation.ImaginaryY != ReferenceCamera.Placement.Rotation.ImaginaryY ||
            Declaring.Placement.Rotation.ImaginaryZ != ReferenceCamera.Placement.Rotation.ImaginaryZ ||
            Declaring.Placement.Rotation.Real       != ReferenceCamera.Placement.Rotation.Real;

        // 📝 A camera that has barely moved owes nothing. The threshold is `Foundation/`'s own altitude
        //    materiality reused, because both answer the same question — how far a viewer must move before a
        //    precomputed thing stops describing what it sees.
        if (Moved <= CameraAltitudeMateriality && !RangeAmended && !RotationAmended)
        {
            ReferenceCamera = Declaring;
            return Deliver<bool>::Result(true);
        }
    }

    ReferenceCamera  = Declaring;
    CameraDeclared  = true;
    SubdivisionOwed = true;

    // 🔴 The camera owes the **directional** subdivision and nothing else — `60` §4's two camera rows. Point,
    //    spot and extended shapes are world-referred and see exactly what they saw before the camera moved; a
    //    projection set that rebuilt all four on a camera move is a workspace that stutters while the artist
    //    orbits, which is the thing they do most.
    for (DerivedProjection& Current : Projections)
    {
        if (Current.Shape == ProjectionShape::Subdivided)
            Current.RebuildOwed = true;
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE INVALIDATION
//------------------------------------------------------------------------------------------------------------------------

std::size_t OcclusionProjectionSpace::Located(OwnerIdentity Illuminant) const
{
    for (std::size_t Index = 0u; Index < Projections.size(); ++Index)
    {
        if (Projections[Index].Illuminant == Illuminant)
            return Index;
    }

    return Projections.size();
}

Deliver<bool> OcclusionProjectionSpace::Invalidate(InvalidationSubject Declared,
                                                   OwnerIdentity    Subject,
                                                   PartitionExtent     Extent)
{
    switch (Declared)
    {
        case InvalidationSubject::IlluminantAmended:
        {
            const std::size_t Located_ = Located(Subject);

            if (Located_ == Projections.size())
            {
                // 📝 An illuminant carrying no projection yet is accepted rather than rejected. It is either
                //    newly registered or newly declared, and `Rebuild` derives it on its next pass — refusing
                //    would make the caller test registration before declaring a change it already made.
                return Deliver<bool>::Result(true);
            }

            Projections[Located_].RebuildOwed = true;

            return Deliver<bool>::Result(true);
        }

        case InvalidationSubject::OwnerMoved:
        case InvalidationSubject::CutoutCoverage:
        {
            // 🔴 Every projection **whose extent reaches it** and no others — `60` §4. A moved owner on the
            //    far side of a scene changes nothing about what a near illuminant sees, and rebuilding every
            //    projection for it is a scene where dragging one object costs what rebuilding the lighting does.
            // ⚠️ Cutout coverage takes the same row and is the exception `62` §2 declares: cutout coverage is
            //    resolved at `16` §3.1, so a cutout owner already occludes correctly here — and a change to
            //    its coverage channel is therefore a change to what a projection sees, unlike every other texture.
            for (DerivedProjection& Current : Projections)
            {
                if (Current.Faces.empty())
                {
                    Current.RebuildOwed = true;
                    continue;
                }

                const DocumentPosition Origin = Current.Faces[0].Projected.ViewOrigin;

                if (ExtentsOverlap(Extent, Origin, Current.Faces[0].FurthestPlane))
                    Current.RebuildOwed = true;
            }

            return Deliver<bool>::Result(true);
        }

        case InvalidationSubject::CameraMoved:
        {
            SubdivisionOwed = true;

            for (DerivedProjection& Current : Projections)
            {
                if (Current.Shape == ProjectionShape::Subdivided)
                    Current.RebuildOwed = true;
            }

            return Deliver<bool>::Result(true);
        }

        case InvalidationSubject::RadiantIntensity:
        case InvalidationSubject::OwnerTextured:
        {
            // 🔴 Nothing. `44` §2's extent is **declared** rather than derived from the magnitude, so brightening
            //    an illuminant cannot enlarge what it reaches; and occlusion reads topology rather than channels,
            //    so a texture stroke changes nothing a projection can see. These are the two things the artist does
            //    constantly, and both are accepted here so a caller may declare every change it makes without
            //    knowing which ones matter — which is the only arrangement in which the ones that do not matter
            //    stay free.
            return Deliver<bool>::Result(true);
        }

        case InvalidationSubject::SubjectCount:
            break;
    }

    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such invalidation subject" });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DERIVATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<DerivedProjection> OcclusionProjectionSpace::Derive(const IlluminantSpecification& Declared,
                                                            OwnerIdentity               Illuminant,
                                                            std::uint64_t                  RecordingIndex) const
{
    DerivedProjection Deriving;
    Deriving.Illuminant   = Illuminant;
    Deriving.Shape        = ShapeOfEmission(Declared.Emission);
    Deriving.ExtentTexels = ProjectionExtentTexels;
    Deriving.DerivedAt    = RecordingIndex;
    Deriving.RebuildOwed  = false;

    if (Deriving.Shape == ProjectionShape::ShapeCount)
        return Deliver<DerivedProjection>::Refuse({ RefusalReason::ContentUnsupported, "no such emission shape" });

    switch (Deriving.Shape)
    {
        case ProjectionShape::SixFaces:
        {
            Deriving.EmissionSize = Declared.EmissionRadius * 2.0;

            for (std::uint32_t FaceIndex = 0u; FaceIndex < 6u; ++FaceIndex)
            {
                CameraSpecification Projecting;
                Projecting.Placement.Translation = Declared.Placement.Translation;
                Projecting.Placement.Rotation    = FacingOf(FaceIndex);
                Projecting.Projected             = ProjectionSubject::Perspective;

                // 📐 Ninety degrees exactly, because six of them tile the whole sphere and a wider face would
                //    overlap its neighbour — which records the same occluder twice at two extents and produces a
                //    visible discontinuity along every one of the six seams.
                Projecting.ExtentParameter  = 90.0;
                Projecting.SensorProportion = 1.0;

                // 📝 The nearest plane sits just outside the emission surface. A plane at the illuminant's own
                //    position divides by nothing in the projection, and a plane far outside it clips away the
                //    contact the whole term exists to resolve.
                Projecting.Clipping.Nearest  = Declared.EmissionRadius > 0.0
                                             ? Declared.EmissionRadius : 1.0;
                Projecting.Clipping.Furthest = Declared.ExtentReach;

                if (!Projecting.Clipping.IntervalValid())
                {
                    return Deliver<DerivedProjection>::Refuse(
                        { RefusalReason::ContentUnsupported, "the declared extent lies inside the emission surface" });
                }

                const Deliver<ViewProjection> Projected = Slate::Derive(Projecting);

                if (!Projected.Resolved)
                    return Deliver<DerivedProjection>::Refuse(Projected.Error);

                ProjectionFace Face;
                Face.Projected     = Projected.Resolve();
                Face.NearestPlane  = Projecting.Clipping.Nearest;
                Face.FurthestPlane = Projecting.Clipping.Furthest;
                Face.Bounding.DeriveFrustumPlanes(Face.Projected);

                Deriving.Faces.push_back(Face);
            }

            break;
        }

        case ProjectionShape::SingleCone:
        {
            Deriving.EmissionSize = Declared.EmissionRadius * 2.0;

            CameraSpecification Projecting;
            Projecting.Placement        = Declared.Placement;
            Projecting.Projected        = ProjectionSubject::Perspective;
            Projecting.SensorProportion = 1.0;

            // 📐 The declared cone plus both margins of its softness, because a penumbra resolved from a
            //    projection that ends at the cone's own edge has no occluder recorded outside it — and the
            //    softened margin then reads as fully lit rather than as partly shadowed.
            const double Spanned = Declared.ConeAngle + 2.0 * Declared.ConeSoftness;

            Projecting.ExtentParameter   = Spanned < 179.0 ? Spanned : 179.0;
            Projecting.Clipping.Nearest  = Declared.EmissionRadius > 0.0 ? Declared.EmissionRadius : 1.0;
            Projecting.Clipping.Furthest = Declared.ExtentReach;

            if (!Projecting.Clipping.IntervalValid())
            {
                return Deliver<DerivedProjection>::Refuse(
                    { RefusalReason::ContentUnsupported, "the declared extent lies inside the emission surface" });
            }

            const Deliver<ViewProjection> Projected = Slate::Derive(Projecting);

            if (!Projected.Resolved)
                return Deliver<DerivedProjection>::Refuse(Projected.Error);

            ProjectionFace Face;
            Face.Projected     = Projected.Resolve();
            Face.NearestPlane  = Projecting.Clipping.Nearest;
            Face.FurthestPlane = Projecting.Clipping.Furthest;
            Face.Bounding.DeriveFrustumPlanes(Face.Projected);

            Deriving.Faces.push_back(Face);

            break;
        }

        case ProjectionShape::SingleAxis:
        {
            // 📐 The extended shape's own diagonal, which is what `44`'s incidence projection already treats as
            //    its effective radius. Using one edge instead would soften along one axis and not the other,
            //    which reads as a shadow that is sharp horizontally and blurred vertically.
            const double HalfWidth  = Declared.ExtendedWidth  * 0.5;
            const double HalfHeight = Declared.ExtendedHeight * 0.5;

            Deriving.EmissionSize = 2.0 * std::sqrt(HalfWidth * HalfWidth + HalfHeight * HalfHeight);

            CameraSpecification Projecting;
            Projecting.Placement         = Declared.Placement;
            Projecting.Projected         = ProjectionSubject::Perspective;
            Projecting.SensorProportion  = 1.0;
            Projecting.ExtentParameter   = 90.0;
            Projecting.Clipping.Nearest  = Deriving.EmissionSize > 0.0 ? Deriving.EmissionSize * 0.5 : 1.0;
            Projecting.Clipping.Furthest = Declared.ExtentReach;

            if (!Projecting.Clipping.IntervalValid())
            {
                return Deliver<DerivedProjection>::Refuse(
                    { RefusalReason::ContentUnsupported, "the declared extent lies inside the emission surface" });
            }

            const Deliver<ViewProjection> Projected = Slate::Derive(Projecting);

            if (!Projected.Resolved)
                return Deliver<DerivedProjection>::Refuse(Projected.Error);

            ProjectionFace Face;
            Face.Projected     = Projected.Resolve();
            Face.NearestPlane  = Projecting.Clipping.Nearest;
            Face.FurthestPlane = Projecting.Clipping.Furthest;
            Face.Bounding.DeriveFrustumPlanes(Face.Projected);

            Deriving.Faces.push_back(Face);

            break;
        }

        default:   // Subdivided
        {
            if (!CameraDeclared)
            {
                return Deliver<DerivedProjection>::Refuse(
                    { RefusalReason::ContentUnsupported, "a directional subdivision needs the camera's own range" });
            }

            // 📐 The angular size becomes a linear one at the slice's own distance, which is the only distance a
            //    parallel projection has. `44` §3 declares a directional shape by its angular size precisely
            //    because it has no position, so the penumbra it casts is proportional to how far the receiver
            //    stands from its occluder — which is what `ProjectPenumbraWidth` already reads.
            Deriving.EmissionSize = Declared.AngularSize * Pi / 180.0;

            const double Nearest  = ReferenceCamera.Clipping.Nearest;
            const double Furthest = ReferenceCamera.Clipping.Furthest;

            for (std::uint32_t Slice = 0u; Slice < DirectionalSubdivisionCount; ++Slice)
            {
                // 📐 Split logarithmically rather than evenly. An even split gives the nearest slice the same
                //    extent as the furthest, and the nearest slice is where every texel is worth ten of the
                //    furthest's — which is a projection whose resolution is highest exactly where the artist
                //    cannot see it.
                const double LowerFraction = static_cast<double>(Slice)      / DirectionalSubdivisionCount;
                const double UpperFraction = static_cast<double>(Slice + 1u) / DirectionalSubdivisionCount;

                const double SliceNearest  = Nearest * std::pow(Furthest / Nearest, LowerFraction);
                const double SliceFurthest = Nearest * std::pow(Furthest / Nearest, UpperFraction);

                DocumentPosition Centre;
                double           Radius = 0.0;
                SliceBounding(ReferenceCamera, SliceNearest, SliceFurthest, Centre, Radius);

                if (Radius <= 0.0)
                    continue;

                SnapSlice(Centre, Radius, Declared.Placement.Rotation, ProjectionExtentTexels);

                double ForwardX = 0.0;
                double ForwardY = 0.0;
                double ForwardZ = 0.0;
                RotateSpan(Declared.Placement.Rotation, 0.0, 0.0, -1.0, ForwardX, ForwardY, ForwardZ);

                CameraSpecification Projecting;
                Projecting.Placement.Rotation    = Declared.Placement.Rotation;
                Projecting.Placement.Translation.PositionX = Centre.PositionX - ForwardX * Radius * 2.0;
                Projecting.Placement.Translation.PositionY = Centre.PositionY - ForwardY * Radius * 2.0;
                Projecting.Placement.Translation.PositionZ = Centre.PositionZ - ForwardZ * Radius * 2.0;

                Projecting.Projected         = ProjectionSubject::Parallel;
                Projecting.SensorProportion  = 1.0;
                Projecting.ExtentParameter   = 2.0 * Radius;
                Projecting.Clipping.Nearest  = Radius * 0.5;
                Projecting.Clipping.Furthest = Radius * 4.0;

                const Deliver<ViewProjection> Projected = Slate::Derive(Projecting);

                if (!Projected.Resolved)
                    return Deliver<DerivedProjection>::Refuse(Projected.Error);

                ProjectionFace Face;
                Face.Projected     = Projected.Resolve();
                Face.NearestPlane  = SliceNearest;
                Face.FurthestPlane = SliceFurthest;
                Face.Bounding.DeriveFrustumPlanes(Face.Projected);

                Deriving.Faces.push_back(Face);
            }

            if (Deriving.Faces.empty())
            {
                return Deliver<DerivedProjection>::Refuse(
                    { RefusalReason::ContentUnsupported, "the camera's range subdivides into no slice" });
            }

            break;
        }
    }

    return Deliver<DerivedProjection>::Result(Deriving);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REBUILD
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OcclusionProjectionSpace::Rebuild(const IlluminantPopulation& Illuminants,
                                                std::uint64_t               RecordingIndex)
{
    if (!CameraDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no camera has been declared" });

    Reported.RebuiltThisRecording = 0u;
    Reported.UnenrolledCount     = 0u;

    std::vector<DerivedProjection> Current;
    Current.reserve(Projections.size());

    for (const OwnerIdentity& Illuminant : Illuminants.Registered())
    {
        const Deliver<IlluminantSpecification> Declared = Illuminants.Resolve(Illuminant);

        if (!Declared.Resolved)
            continue;

        // 🔴 An unenrolled illuminant is **counted** and skipped, never projected. `60` §3 declares that it is
        //    integrated unattenuated and that this is a behaviour rather than a failure; counting it is what
        //    lets `86` present how many of a scene's illuminants cast nothing at all, which an artist who
        //    disabled occlusion on their key light six months ago has no other route to.
        if (!Declared.Resolve().OcclusionRegistered)
        {
            ++Reported.UnenrolledCount;
            continue;
        }

        const std::size_t Located_ = Located(Illuminant);

        const bool Owed = Located_ == Projections.size() || Projections[Located_].RebuildOwed;

        if (!Owed)
        {
            Current.push_back(Projections[Located_]);
            continue;
        }

        const Deliver<DerivedProjection> Derived = Derive(Declared.Resolve(), Illuminant, RecordingIndex);

        if (!Derived.Resolved)
            return Deliver<bool>::Refuse(Derived.Error);

        Current.push_back(Derived.Resolve());

        ++Reported.RebuiltThisRecording;
        ++Reported.RebuiltTotal;
    }

    Projections.swap(Current);
    SubdivisionOwed = false;

    Reported.ProjectionCount = static_cast<std::uint32_t>(Projections.size());
    Reported.FaceCount       = 0u;

    for (const DerivedProjection& Held : Projections)
        Reported.FaceCount += static_cast<std::uint32_t>(Held.Faces.size());

    Reported.TruncatedTotal = PackedIndex.TruncatedTotal();

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE READS
//------------------------------------------------------------------------------------------------------------------------

Deliver<const DerivedProjection*> OcclusionProjectionSpace::Current(OwnerIdentity Illuminant) const
{
    const std::size_t Located_ = Located(Illuminant);

    if (Located_ == Projections.size())
    {
        return Deliver<const DerivedProjection*>::Refuse(
            { RefusalReason::ExtentExhausted, "the illuminant carries no projection; integrate it unattenuated" });
    }

    return Deliver<const DerivedProjection*>::Result(&Projections[Located_]);
}

bool OcclusionProjectionSpace::RebuildOwed() const
{
    if (SubdivisionOwed)
        return true;

    for (const DerivedProjection& Held : Projections)
    {
        if (Held.RebuildOwed)
            return true;
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORTING
//------------------------------------------------------------------------------------------------------------------------

void OcclusionProjectionSpace::Report(ReportSequence& Reporting,
                                      MeasureIndex&   Measured,
                                      TickPoint       Sampled) const
{
    // 🔴 `86` §4's `60` §3.1 row. Coalesced by **partition ordinal** as the subject, so twelve partitions that
    //    each truncated present as twelve entries rather than as one with a count of twelve — `86` §6 refuses
    //    the second shape by name, because the count is exactly what destroys the fact the artist needs.
    for (std::uint32_t PartitionIndex = 0u; PartitionIndex < PackedIndex.SpannedCount(); ++PartitionIndex)
    {
        if (PackedIndex.TruncatedCount(PartitionIndex) == 0u)
            continue;

        ReportSpecification Truncated;
        Truncated.Origin         = "60 §3.1 OcclusionProjection";
        Truncated.Subject        = "PackedCapacity";
        Truncated.Detail         = "illuminants beyond the packed word are integrated unattenuated, not dropped";
        Truncated.SubjectIndex = PartitionIndex;
        Truncated.Verdict    = ReportVerdict::Truncated;
        Truncated.Arrival        = Sampled;

        Reporting.Append(Truncated);
    }

    // 🔴 Every count **overwrites** — `86` §2. A rebuild count appended once per rotation would bury the one
    //    truncation the artist did not expect under a thousand readings nobody asked for, and `60` appends
    //    exactly one row to `86` §4's register for exactly that reason.
    Measured.DeclareCount("60 §3 OcclusionProjection", "Projections", Reported.ProjectionCount, Sampled);
    Measured.DeclareCount("60 §3 OcclusionProjection", "Faces", Reported.FaceCount, Sampled);
    Measured.DeclareCount("60 §4 OcclusionProjection", "RebuiltThisRecording", Reported.RebuiltThisRecording, Sampled);
    Measured.DeclareCount("60 §4 OcclusionProjection", "RebuiltTotal", Reported.RebuiltTotal, Sampled);
    Measured.DeclareCount("60 §3 OcclusionProjection", "Unenrolled", Reported.UnenrolledCount, Sampled);
    Measured.DeclareCount("60 §3.1 OcclusionProjection", "Truncated", PackedIndex.TruncatedTotal(), Sampled);
}

OcclusionIndex&                 OcclusionProjectionSpace::Index()         { return PackedIndex; }
const OcclusionIndex&           OcclusionProjectionSpace::Index() const   { return PackedIndex; }
AmbientOcclusionSequence&       OcclusionProjectionSpace::Ambient()       { return AmbientTerm; }
const AmbientOcclusionSequence& OcclusionProjectionSpace::Ambient() const { return AmbientTerm; }
const OcclusionMetrics&         OcclusionProjectionSpace::Metrics() const { return Reported;    }

std::uint32_t OcclusionProjectionSpace::ProjectionCount() const
{
    return static_cast<std::uint32_t>(Projections.size());
}

}   // namespace Slate
