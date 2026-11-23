//============================================================================================================================================
//                                                          PARTITIONCLASSIFIER.CPP
//============================================================================================================================================
// 🧩 Eight corners through a projective transform, one cone against one bounding sphere, and the standing the two amount to.

#include "SlateCompute/Compute/VisibilityIndex/Api/PartitionClassifier.h"
#include "Foundation/NumericTolerance.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PROJECTED EXTENT
//------------------------------------------------------------------------------------------------------------------------

ClassifiedPartition ProjectPartitionExtent(const ProjectedTransform&  Composed,
                                           const ConditionedExtent&   Bounded,
                                           std::uint32_t              DisplayX,
                                           std::uint32_t              DisplayY)
{
    ClassifiedPartition Projected;

    if (DisplayX == 0u || DisplayY == 0u)
        return Projected;

    // 📝 The eight corners as the three coordinate choices, read off the low three bits of the corner ordinal. An
    //    explicit table of eight positions says the same thing in eight lines that each have to be checked.
    const double CoordinateX[2] = { Bounded.Minimum.PositionX, Bounded.Maximum.PositionX };
    const double CoordinateY[2] = { Bounded.Minimum.PositionY, Bounded.Maximum.PositionY };
    const double CoordinateZ[2] = { Bounded.Minimum.PositionZ, Bounded.Maximum.PositionZ };

    double MinimumX    =  1.0;
    double MaximumX = -1.0;
    double MinimumY   =  1.0;
    double MaximumY = -1.0;
    double NearestDepth  =  0.0;

    bool StraddlesNearPlane = false;
    bool AnyCornerResolved  = false;

    for (std::uint32_t Corner = 0u; Corner < 8u; ++Corner)
    {
        const double PositionX = CoordinateX[Corner        & 1u];
        const double PositionY = CoordinateY[(Corner >> 1) & 1u];
        const double PositionZ = CoordinateZ[(Corner >> 2) & 1u];

        // 📐 Column-major, matching `ProjectedTransform` — the coefficient at column `c` and row `r` sits at
        //    `4c + r`, so a column is contiguous and the product accumulates one column at a time.
        const double ClipX  = Composed.Coefficient[0]  * PositionX + Composed.Coefficient[4]  * PositionY
                                + Composed.Coefficient[8]  * PositionZ + Composed.Coefficient[12];
        const double ClipY = Composed.Coefficient[1]  * PositionX + Composed.Coefficient[5]  * PositionY
                                + Composed.Coefficient[9]  * PositionZ + Composed.Coefficient[13];
        const double ClipDepth  = Composed.Coefficient[2]  * PositionX + Composed.Coefficient[6]  * PositionY
                                + Composed.Coefficient[10] * PositionZ + Composed.Coefficient[14];
        const double ClipScale  = Composed.Coefficient[3]  * PositionX + Composed.Coefficient[7]  * PositionY
                                + Composed.Coefficient[11] * PositionZ + Composed.Coefficient[15];

        // 🔴 At or behind the nearest plane. Dividing here reflects the corner through the origin and the extent
        //    that results bounds nothing; the whole extent is reported instead, below.
        if (ClipScale <= FrustumOutwardMargin)
        {
            StraddlesNearPlane = true;
            continue;
        }

        const double ResolvedX  = ClipX  / ClipScale;
        const double ResolvedY = ClipY / ClipScale;
        const double ResolvedDepth  = ClipDepth  / ClipScale;

        if (!AnyCornerResolved)
        {
            MinimumX     = ResolvedX;
            MaximumX  = ResolvedX;
            MinimumY    = ResolvedY;
            MaximumY = ResolvedY;
            NearestDepth   = ResolvedDepth;

            AnyCornerResolved = true;

            continue;
        }

        MinimumX     = ResolvedX  < MinimumX     ? ResolvedX  : MinimumX;
        MaximumX  = ResolvedX  > MaximumX  ? ResolvedX  : MaximumX;
        MinimumY    = ResolvedY < MinimumY    ? ResolvedY : MinimumY;
        MaximumY = ResolvedY > MaximumY ? ResolvedY : MaximumY;

        // 📐 Reversed depth — the nearest coordinate is the greatest. This is the operand the occlusion comparison
        //    reads, and taking the least here rejects a partition whose front face stands in front of everything.
        NearestDepth = ResolvedDepth > NearestDepth ? ResolvedDepth : NearestDepth;
    }

    if (!AnyCornerResolved)
        return Projected;

    if (StraddlesNearPlane)
    {
        // ⚠️ Conservative in the only direction that is safe. A straddling extent covers an unbounded region of
        //    the display in the limit, so it is tested at the coarsest level and rejected by nothing but depth.
        Projected.OriginX     = 0u;
        Projected.OriginY    = 0u;
        Projected.ProjectedX  = DisplayX;
        Projected.ProjectedY = DisplayY;
        Projected.NearestDepth    = static_cast<float>(NearestDepth);

        return Projected;
    }

    // 📐 Clip ordinates run from −1 to 1 along both display axes, so half the span is the fraction of the
    //    display the extent covers. Rounded outward by a ceiling, because a level chosen from an extent rounded
    //    down is one a two-by-two reading does not span.
    const double SpannedX  = (MaximumX  - MinimumX)  * 0.5 * static_cast<double>(DisplayX);
    const double SpannedY = (MaximumY - MinimumY) * 0.5 * static_cast<double>(DisplayY);

    const double WidenedX  = std::ceil(SpannedX);
    const double WidenedY = std::ceil(SpannedY);

    // 📐 The origin is the least clip coordinate carried into texels and rounded **down**, which widens the covered
    //    region in the same direction the ceiling above does. The second display coordinate is already inverted by
    //    the projection — `ClipCoordinateSignum` is applied in `46`'s second row — so the least clip coordinate is
    //    the least texel on both axes and no second inversion belongs here.
    const double BeganX  = std::floor((MinimumX  + 1.0) * 0.5 * static_cast<double>(DisplayX));
    const double BeganY = std::floor((MinimumY + 1.0) * 0.5 * static_cast<double>(DisplayY));

    // 📝 Clamped into the display rather than rejected. A partition partly off one edge projects a least coordinate
    //    below −1, and its origin is then the first texel of the display it does reach; the span above already
    //    covers the whole of what it projects to, so the four texels read still enclose it.
    Projected.OriginX  = BeganX  > 0.0
                           ? (BeganX  > static_cast<double>(DisplayX  - 1u)
                              ? DisplayX  - 1u : static_cast<std::uint32_t>(BeganX))
                           : 0u;
    Projected.OriginY = BeganY > 0.0
                           ? (BeganY > static_cast<double>(DisplayY - 1u)
                              ? DisplayY - 1u : static_cast<std::uint32_t>(BeganY))
                           : 0u;

    Projected.ProjectedX  = WidenedX  > 0.0
                              ? (WidenedX  > static_cast<double>(DisplayX)
                                 ? DisplayX  : static_cast<std::uint32_t>(WidenedX))
                              : 0u;
    Projected.ProjectedY = WidenedY > 0.0
                              ? (WidenedY > static_cast<double>(DisplayY)
                                 ? DisplayY : static_cast<std::uint32_t>(WidenedY))
                              : 0u;
    Projected.NearestDepth    = static_cast<float>(NearestDepth);

    return Projected;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE ORIENTATION CONE
//------------------------------------------------------------------------------------------------------------------------

bool OrientationRejected(const OrientationCone&    Turned,
                         const ConditionedExtent&  Bounded,
                         DocumentPosition          ViewOrigin)
{
    // 🔴 A partition whose orientations exceed a hemisphere has no cone, and no direction exists from which every
    //    face of it is back-facing. Rejecting on an undrived cone is how a partition is culled while the artist
    //    is looking straight at part of it.
    if (!Turned.ConeDerived)
        return false;

    const double CentreX = (Bounded.Minimum.PositionX + Bounded.Maximum.PositionX) * 0.5;
    const double CentreY = (Bounded.Minimum.PositionY + Bounded.Maximum.PositionY) * 0.5;
    const double CentreZ = (Bounded.Minimum.PositionZ + Bounded.Maximum.PositionZ) * 0.5;

    const double HalfX = (Bounded.Maximum.PositionX - Bounded.Minimum.PositionX) * 0.5;
    const double HalfY = (Bounded.Maximum.PositionY - Bounded.Minimum.PositionY) * 0.5;
    const double HalfZ = (Bounded.Maximum.PositionZ - Bounded.Minimum.PositionZ) * 0.5;

    const double BoundingRadius = std::sqrt(HalfX * HalfX + HalfY * HalfY + HalfZ * HalfZ);

    const double TowardX = CentreX - ViewOrigin.PositionX;
    const double TowardY = CentreY - ViewOrigin.PositionY;
    const double TowardZ = CentreZ - ViewOrigin.PositionZ;

    const double TowardLength = std::sqrt(TowardX * TowardX + TowardY * TowardY + TowardZ * TowardZ);

    // 🔴 The camera stands inside the bounding sphere. The directions to the partition then span more than a
    //    hemisphere, no cone excludes them, and the half-angle below has no value — so nothing is rejected.
    // 📐 This is also the whole of the guard the division below needs. The radius is a square root and so never
    //    negative, which makes a zero length one this comparison already answers; every length reaching the
    //    normalisation is strictly greater than a non-negative number and therefore strictly positive.
    if (TowardLength <= BoundingRadius)
        return false;

    const double ViewingX = TowardX / TowardLength;
    const double ViewingY = TowardY / TowardLength;
    const double ViewingZ = TowardZ / TowardLength;

    const double Agreement = ViewingX * static_cast<double>(Turned.Axis.DirectionX)
                           + ViewingY * static_cast<double>(Turned.Axis.DirectionY)
                           + ViewingZ * static_cast<double>(Turned.Axis.DirectionZ);

    // 📐 `ApertureCosine` is the cosine of the cone's half-angle, so its sine is the widening the cone itself
    //    contributes. Clamped before the square root because a cosine one part in 10¹⁶ above unity is a negative
    //    radicand, and the resulting quiet NaN compares false against everything — a cull that silently stops.
    const double ApertureCosine = static_cast<double>(Turned.ApertureCosine) < -1.0
                                ? -1.0
                                : (static_cast<double>(Turned.ApertureCosine) > 1.0
                                   ? 1.0 : static_cast<double>(Turned.ApertureCosine));

    const double ApertureSine  = std::sqrt(1.0 - ApertureCosine * ApertureCosine);
    const double SubtendedSine = BoundingRadius / TowardLength;

    return Agreement > ApertureSine + SubtendedSine;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE FIRST PHASE
//------------------------------------------------------------------------------------------------------------------------

ClassifiedPartition ClassifyPartition(const MicroSurfacePartition&  Partitioned,
                                      const ViewProjection&         Viewing,
                                      const FrustumSpace&           Bounding,
                                      const ProjectedTransform&     Composed,
                                      std::uint32_t                 PartitionIndex,
                                      std::uint32_t                 FirstTriangle,
                                      std::uint32_t                 DisplayX,
                                      std::uint32_t                 DisplayY)
{
    ClassifiedPartition Classified;
    Classified.PartitionIndex = PartitionIndex;
    Classified.FirstTriangle    = FirstTriangle;
    Classified.TriangleCount    = Partitioned.TriangleCount;

    // 📝 Cheapest predicate first, and the one that rejects the most. `Classify` answers −1 for wholly outside,
    //    which is the only standing that excludes; straddling is drawn and clipped by the hardware.
    if (Bounding.Classify(Partitioned.Extent.Minimum, Partitioned.Extent.Maximum) < 0)
    {
        Classified.Current = PartitionVerdict::FrustumExcluded;

        return Classified;
    }

    if (OrientationRejected(Partitioned.Orientation, Partitioned.Extent, Viewing.ViewOrigin))
    {
        Classified.Current = PartitionVerdict::OrientationExcluded;

        return Classified;
    }

    // 📝 The run is carried across from the standing above rather than assigned twice. `ProjectPartitionExtent`
    //    answers about the extent alone and knows nothing of the fan, so what it returns carries no run at all.
    ClassifiedPartition Accepted = ProjectPartitionExtent(Composed, Partitioned.Extent, DisplayX, DisplayY);
    Accepted.Current         = PartitionVerdict::Accepted;
    Accepted.PartitionIndex = Classified.PartitionIndex;
    Accepted.FirstTriangle    = Classified.FirstTriangle;
    Accepted.TriangleCount    = Classified.TriangleCount;

    return Accepted;
}

}   // namespace Slate
