//============================================================================================================================================
//                                                        POINTERINTERSECTION.CPP
//============================================================================================================================================
// 🧩 The unprojection, the one traversal that resolves the whole tuple, and the marquee as a narrower camera.

#include "SlateDocument/Document/PointerIntersection/Api/PointerIntersection.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSFORM HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The quaternion sandwich, expanded. `SpatialSubdivision`, `DecalProjection` and `CameraProjection` each spell
//    it too, because `02`'s `TransformProjection` exports a matrix derivation and no direction rotation — and
//    deriving a matrix to turn one direction is the same arithmetic with a temporary in the middle.
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

// 📝 Ordered by slot and then by generation, so the ordering is total over identities and survives a slot being
//    reused. `IlluminantPopulation` orders its own storage the same way and for the same reason.
bool PrecedesInIdentity(OwnerIdentity Earlier, OwnerIdentity Later)
{
    if (Earlier.SlotIndex != Later.SlotIndex)
        return Earlier.SlotIndex < Later.SlotIndex;

    return Earlier.SlotGeneration < Later.SlotGeneration;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE UNPROJECTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<ProjectedRay> ProjectPointerRay(const CameraProjection& Camera,
                                        double                  PointerX,
                                        double                  PointerY,
                                        std::uint32_t           DisplayX,
                                        std::uint32_t           DisplayY)
{
    if (DisplayX == 0u || DisplayY == 0u)
        return Deliver<ProjectedRay>::Refuse({ RefusalReason::ContentUnsupported, "a display extent of zero" });

    // 🔴 A camera owing a reconciliation refuses. `46` §7 makes `Reconcile` the only writer of the derivation,
    //    and casting through the standing one aims the ray where the artist was looking before they moved.
    if (Camera.DerivationOwed())
    {
        return Deliver<ProjectedRay>::Refuse(
            { RefusalReason::HostDenied, "the camera owes a reconciliation; its projection is stale" });
    }

    const CameraSpecification& Declared  = Camera.Declared();
    const double*              Projected = Camera.Projected().Projected.Coefficient;

    const double XScale  = Projected[0];
    const double YScale = Projected[5];

    if (XScale == 0.0 || YScale == 0.0)
    {
        return Deliver<ProjectedRay>::Refuse(
            { RefusalReason::ContentUnsupported, "the projection resolves no interior on one axis" });
    }

    // 📐 The clip coordinate is already inverted by `ClipCoordinateSignum` in the projection's second row, so the
    //    display's downward-increasing coordinate maps directly and no second inversion belongs here.
    const double ClipX  = 2.0 * PointerX  / static_cast<double>(DisplayX)  - 1.0;
    const double ClipY = 2.0 * PointerY / static_cast<double>(DisplayY) - 1.0;

    const double ViewX  = ClipX  / XScale;
    const double ViewY = ClipY / YScale;

    ProjectedRay Ray;

    double DirectionX = 0.0;
    double DirectionY = 0.0;
    double DirectionZ = 0.0;

    if (Declared.Projected == ProjectionSubject::Perspective)
    {
        // 📐 Every ray leaves the camera's own position and the view-space direction is the clip position at
        //    unit depth. Normalising here is what makes `40`'s returned parameter a document-space distance.
        const double Length = std::sqrt(ViewX * ViewX + ViewY * ViewY + 1.0);

        RotateSpan(Declared.Placement.Rotation,
                   ViewX / Length, ViewY / Length, -1.0 / Length,
                   DirectionX, DirectionY, DirectionZ);

        Ray.Origin = Declared.Placement.Translation;
    }
    else
    {
        // 📐 A parallel projection displaces the origin and keeps one direction. The view direction is `46` §3's
        //    convention — the camera's own negative third axis — and it is already unit length.
        double OffsetX = 0.0;
        double OffsetY = 0.0;
        double OffsetZ = 0.0;
        RotateSpan(Declared.Placement.Rotation, ViewX, ViewY, 0.0, OffsetX, OffsetY, OffsetZ);

        RotateSpan(Declared.Placement.Rotation, 0.0, 0.0, -1.0, DirectionX, DirectionY, DirectionZ);

        Ray.Origin.PositionX = Declared.Placement.Translation.PositionX + OffsetX;
        Ray.Origin.PositionY = Declared.Placement.Translation.PositionY + OffsetY;
        Ray.Origin.PositionZ = Declared.Placement.Translation.PositionZ + OffsetZ;
    }

    Ray.DirectionX = DirectionX;
    Ray.DirectionY = DirectionY;
    Ray.DirectionZ = DirectionZ;

    return Deliver<ProjectedRay>::Result(Ray);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ADMISSIONS
//------------------------------------------------------------------------------------------------------------------------

std::size_t PointerIntersection::Located(OwnerIdentity Subject) const
{
    std::size_t Lower = 0u;
    std::size_t Upper = Surfaces.size();

    while (Lower < Upper)
    {
        const std::size_t Middle = Lower + (Upper - Lower) / 2u;

        if (PrecedesInIdentity(Surfaces[Middle].Owner, Subject))
            Lower = Middle + 1u;
        else
            Upper = Middle;
    }

    return Lower;
}

Deliver<bool> PointerIntersection::Accept(const AcceptedSurface& Incoming)
{
    if (!Incoming.Owner.IdentityDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "an undeclared identity accepts nothing" });

    // 🔴 Confirmed here rather than at the hit. A run one corner short reads past its end at whichever triangle
    //    happens to touch the last corner, which is a pick that is correct almost everywhere — and the artist
    //    meets it as one face of a model texturing somewhere else.
    if (Incoming.Imported != nullptr && Incoming.CornerCoordinates != nullptr
     && static_cast<std::uint32_t>(Incoming.CornerCoordinates->size()) != Incoming.Imported->CornerCount())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the coordinate run carries a corner count the topology does not" });
    }

    const std::size_t Located_ = Located(Incoming.Owner);

    if (Located_ < Surfaces.size() && Surfaces[Located_].Owner == Incoming.Owner)
        Surfaces[Located_] = Incoming;
    else
        Surfaces.insert(Surfaces.begin() + static_cast<std::ptrdiff_t>(Located_), Incoming);

    return Deliver<bool>::Result(true);
}

Deliver<bool> PointerIntersection::Withdraw(OwnerIdentity Subject)
{
    const std::size_t Located_ = Located(Subject);

    if (Located_ >= Surfaces.size() || !(Surfaces[Located_].Owner == Subject))
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the owner is not accepted here" });

    Surfaces.erase(Surfaces.begin() + static_cast<std::ptrdiff_t>(Located_));

    return Deliver<bool>::Result(true);
}

Deliver<const AcceptedSurface*> PointerIntersection::Current(OwnerIdentity Subject) const
{
    const std::size_t Located_ = Located(Subject);

    if (Located_ >= Surfaces.size() || !(Surfaces[Located_].Owner == Subject))
    {
        return Deliver<const AcceptedSurface*>::Refuse(
            { RefusalReason::IdentityStale, "the owner is not accepted here" });
    }

    return Deliver<const AcceptedSurface*>::Result(&Surfaces[Located_]);
}

std::uint32_t PointerIntersection::AcceptedCount() const
{
    return static_cast<std::uint32_t>(Surfaces.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE POINTER RAY
//------------------------------------------------------------------------------------------------------------------------

ResolvedPointer PointerIntersection::Resolve(const ProjectedRay&    Projected,
                                             const OctantSpace&     Subdivision,
                                             const RegistrationIndex& Subsets,
                                             const PlacementIndex&  Placements) const
{
    ResolvedPointer Pointed;

    const ResolvedIntersection Met = Subdivision.IntersectRay(Projected.Origin,
                                                              Projected.DirectionX,
                                                              Projected.DirectionY,
                                                              Projected.DirectionZ,
                                                              Subsets);

    if (!Met.Resolved)
        return Pointed;

    Pointed.Owner          = Met.Owner;
    Pointed.FaceIndex       = Met.FaceIndex;
    Pointed.CornerIndexs[0] = Met.CornerIndexs[0];
    Pointed.CornerIndexs[1] = Met.CornerIndexs[1];
    Pointed.CornerIndexs[2] = Met.CornerIndexs[2];
    Pointed.Weights[0]        = Met.Weights[0];
    Pointed.Weights[1]        = Met.Weights[1];
    Pointed.Weights[2]        = Met.Weights[2];
    Pointed.Distance          = Met.Distance;
    Pointed.Position          = Met.Position;
    Pointed.Resolved          = true;

    const Deliver<const AcceptedSurface*> Accepted = Current(Met.Owner);

    if (!Accepted.Resolved || Accepted.Resolve()->Imported == nullptr)
        return Pointed;

    const AcceptedSurface&   Surfaced = *Accepted.Resolve();
    const TopologyStructure& Imported = *Surfaced.Imported;

    // 📐 The hit face's own flat perpendicular, from the three corner positions the traversal named. `38`'s
    //    interpolated shading perpendicular is the wrong operand here: `78` §2 fixes a drag plane at Open, and a
    //    plane that follows shading curvature slides under the manipulator as the pointer moves across it.
    const std::vector<DocumentPosition>& Positions = Imported.Positions();

    const DocumentPosition& Alpha = Positions[Imported.CornerVertex(Met.CornerIndexs[0])];
    const DocumentPosition& Beta  = Positions[Imported.CornerVertex(Met.CornerIndexs[1])];
    const DocumentPosition& Gamma = Positions[Imported.CornerVertex(Met.CornerIndexs[2])];

    const double FirstX  = Beta.PositionX  - Alpha.PositionX;
    const double FirstY  = Beta.PositionY  - Alpha.PositionY;
    const double FirstZ  = Beta.PositionZ  - Alpha.PositionZ;
    const double SecondX = Gamma.PositionX - Alpha.PositionX;
    const double SecondY = Gamma.PositionY - Alpha.PositionY;
    const double SecondZ = Gamma.PositionZ - Alpha.PositionZ;

    const double PerpX = FirstY * SecondZ - FirstZ * SecondY;
    const double PerpY = FirstZ * SecondX - FirstX * SecondZ;
    const double PerpZ = FirstX * SecondY - FirstY * SecondX;

    const double PerpLength = std::sqrt(PerpX * PerpX + PerpY * PerpY + PerpZ * PerpZ);

    const Deliver<AcceptedOwner> Occupying = Subdivision.Current(Met.Owner);

    if (PerpLength > 0.0 && Occupying.Resolved)
    {
        double TurnedX = 0.0;
        double TurnedY = 0.0;
        double TurnedZ = 0.0;
        RotateSpan(Occupying.Resolve().Composed.Rotation,
                   PerpX / PerpLength, PerpY / PerpLength, PerpZ / PerpLength,
                   TurnedX, TurnedY, TurnedZ);

        Pointed.Orientation.DirectionX = static_cast<float>(TurnedX);
        Pointed.Orientation.DirectionY = static_cast<float>(TurnedY);
        Pointed.Orientation.DirectionZ = static_cast<float>(TurnedZ);
    }

    if (Surfaced.CornerCoordinates == nullptr)
        return Pointed;

    const std::vector<DomainCoordinate>& Coordinates = *Surfaced.CornerCoordinates;

    // 📐 The barycentric weights the traversal produced, applied to the corners' own domain coordinates. `40`
    //    fan-triangulates from a face's first corner and so does `38` §4, so the three corners named here index
    //    the same run the conditioning and the unwrap both addressed.
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const DomainCoordinate& Held = Coordinates[Met.CornerIndexs[Index]];

        Pointed.DomainX  += Met.Weights[Index] * static_cast<double>(Held.CoordinateX);
        Pointed.DomainY += Met.Weights[Index] * static_cast<double>(Held.CoordinateY);
    }

    Pointed.DomainResolved = true;

    if (Surfaced.Placements == nullptr)
        return Pointed;

    // 🔴 `74` §3's precedence 1, resolved after the traversal because it needs the domain position the traversal
    //    produced. The extent contains the position and the source is then confirmed, because `72` rounds the
    //    extent outward and a rotated placement's extent is larger than the placement — the extent alone would
    //    pick a decal whose corner the cursor is beside, and `26` §5 outlines the placement rather than its
    //    extent, so the two would disagree about what the artist selected.
    const Deliver<std::uint32_t> Contained =
        Surfaced.Placements->Resolve(Pointed.DomainX, Pointed.DomainY);

    if (!Contained.Resolved)
        return Pointed;

    const Deliver<const PlacementSpecification*> Placed = Placements.Resolve(Contained.Resolve());

    if (!Placed.Resolved)
        return Pointed;

    double SourceX  = 0.0;
    double SourceY = 0.0;

    if (!ProjectIntoSource(*Placed.Resolve(), Pointed.DomainX, Pointed.DomainY,
                           SourceX, SourceY))
    {
        return Pointed;
    }

    Pointed.PlacementIndex  = Contained.Resolve();
    Pointed.PlacementResolved = true;

    return Pointed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE MARQUEE
//------------------------------------------------------------------------------------------------------------------------

std::vector<OwnerIdentity> PointerIntersection::ResolveExtent(const CameraProjection& Camera,
                                                                 double                  MinimumX,
                                                                 double                  MinimumY,
                                                                 double                  MaximumX,
                                                                 double                  MaximumY,
                                                                 std::uint32_t           DisplayX,
                                                                 std::uint32_t           DisplayY,
                                                                 bool                    ContainmentDeclared,
                                                                 const OctantSpace&      Subdivision,
                                                                 const RegistrationIndex&  Subsets) const
{
    std::vector<OwnerIdentity> Registered;

    if (DisplayX == 0u || DisplayY == 0u || Camera.DerivationOwed())
        return Registered;

    const double ClipLeft     = 2.0 * MinimumX     / static_cast<double>(DisplayX)  - 1.0;
    const double ClipRight  = 2.0 * MaximumX  / static_cast<double>(DisplayX)  - 1.0;
    const double ClipTop    = 2.0 * MinimumY    / static_cast<double>(DisplayY) - 1.0;
    const double ClipBottom = 2.0 * MaximumY / static_cast<double>(DisplayY) - 1.0;

    const double Width  = ClipRight  - ClipLeft;
    const double Height = ClipBottom - ClipTop;

    if (Width <= 0.0 || Height <= 0.0)
        return Registered;

    // 📐 A screen rectangle is a **narrower camera**. The sub-projection's first two rows are the camera's own,
    //    scaled and offset onto the rectangle's clip bounds; every other row is untouched, so the reversed depth
    //    convention and the clipping interval both carry over unamended. `46`'s plane extraction is then read as
    //    it stands, which is what stops the marquee frustum and the culling frustum disagreeing about a plane.
    const ViewProjection& Current = Camera.Projected();

    ViewProjection Narrowed = Current;

    const double XScale   = 2.0 / Width;
    const double XOffset  = -(ClipRight + ClipLeft) / Width;
    const double YScale  = 2.0 / Height;
    const double YOffset = -(ClipBottom + ClipTop) / Height;

    for (std::uint32_t Column = 0u; Column < 4u; ++Column)
    {
        const double XRow  = Current.Projected.Coefficient[Column * 4u];
        const double YRow = Current.Projected.Coefficient[Column * 4u + 1u];
        const double ScaleRow  = Current.Projected.Coefficient[Column * 4u + 3u];

        Narrowed.Projected.Coefficient[Column * 4u]      = XScale  * XRow  + XOffset  * ScaleRow;
        Narrowed.Projected.Coefficient[Column * 4u + 1u] = YScale * YRow + YOffset * ScaleRow;
    }

    for (std::uint32_t Column = 0u; Column < 4u; ++Column)
    {
        for (std::uint32_t Row = 0u; Row < 4u; ++Row)
        {
            double Accumulated = 0.0;

            for (std::uint32_t Passed = 0u; Passed < 4u; ++Passed)
            {
                Accumulated += Narrowed.Projected.Coefficient[Passed * 4u + Row]
                             * Current.ViewRotation.Coefficient[Column * 4u + Passed];
            }

            Narrowed.Composed.Coefficient[Column * 4u + Row] = Accumulated;
        }
    }

    FrustumSpace Marquee;
    Marquee.DeriveFrustumPlanes(Narrowed);

    // 📐 The eight sub-frustum corners, at the two clipping planes, taken in the camera's own frame and carried
    //    into document space. `40` subdivides extents rather than frusta, so the axis-aligned bound is what
    //    narrows the traversal and the six planes are what decide the answer.
    const CameraSpecification& Declared    = Camera.Declared();
    const double               XScale_ = Narrowed.Projected.Coefficient[0];
    const double               YScale_ = Narrowed.Projected.Coefficient[5];

    if (XScale_ == 0.0 || YScale_ == 0.0)
        return Registered;

    ConditionedExtent Bounding;
    bool              FirstAdmission = true;

    const double ClipDistances[2] = { Declared.Clipping.Nearest, Declared.Clipping.Furthest };
    const double ClipCorners[2]   = { -1.0, 1.0 };

    for (std::uint32_t Depth = 0u; Depth < 2u; ++Depth)
    {
        for (std::uint32_t Corner = 0u; Corner < 4u; ++Corner)
        {
            const double Distance   = ClipDistances[Depth];
            const double CornerX    = ClipCorners[Corner & 1u]  / XScale_;
            const double CornerY    = ClipCorners[(Corner >> 1) & 1u] / YScale_;

            const double ViewX = Declared.Projected == ProjectionSubject::Perspective ? CornerX * Distance : CornerX;
            const double ViewY = Declared.Projected == ProjectionSubject::Perspective ? CornerY * Distance : CornerY;

            double TurnedX = 0.0;
            double TurnedY = 0.0;
            double TurnedZ = 0.0;
            RotateSpan(Declared.Placement.Rotation, ViewX, ViewY, -Distance, TurnedX, TurnedY, TurnedZ);

            const double PositionX = Declared.Placement.Translation.PositionX + TurnedX;
            const double PositionY = Declared.Placement.Translation.PositionY + TurnedY;
            const double PositionZ = Declared.Placement.Translation.PositionZ + TurnedZ;

            if (FirstAdmission)
            {
                Bounding.Minimum.PositionX    = PositionX;
                Bounding.Minimum.PositionY    = PositionY;
                Bounding.Minimum.PositionZ    = PositionZ;
                Bounding.Maximum           = Bounding.Minimum;
                FirstAdmission              = false;

                continue;
            }

            Bounding.Minimum.PositionX    = PositionX < Bounding.Minimum.PositionX    ? PositionX : Bounding.Minimum.PositionX;
            Bounding.Minimum.PositionY    = PositionY < Bounding.Minimum.PositionY    ? PositionY : Bounding.Minimum.PositionY;
            Bounding.Minimum.PositionZ    = PositionZ < Bounding.Minimum.PositionZ    ? PositionZ : Bounding.Minimum.PositionZ;
            Bounding.Maximum.PositionX = PositionX > Bounding.Maximum.PositionX ? PositionX : Bounding.Maximum.PositionX;
            Bounding.Maximum.PositionY = PositionY > Bounding.Maximum.PositionY ? PositionY : Bounding.Maximum.PositionY;
            Bounding.Maximum.PositionZ = PositionZ > Bounding.Maximum.PositionZ ? PositionZ : Bounding.Maximum.PositionZ;
        }
    }

    // 📝 The broad phase asks for **overlap** in both modes. An owner wholly inside the frustum is wholly
    //    inside the frustum's own bound, so a containment broad phase would be sound — but it would also refuse
    //    every owner the bound merely clips, and the exact classification below is what should decide that.
    const std::vector<OwnerIdentity> Candidates = Subdivision.IntersectExtent(Bounding, false, Subsets);

    for (const OwnerIdentity& Candidate : Candidates)
    {
        const Deliver<AcceptedOwner> Occupying = Subdivision.Current(Candidate);

        if (!Occupying.Resolved)
            continue;

        const std::int32_t Classified = Marquee.Classify(Occupying.Resolve().Extent.Minimum,
                                                         Occupying.Resolve().Extent.Maximum);

        if (ContainmentDeclared ? Classified > 0 : Classified >= 0)
            Registered.push_back(Candidate);
    }

    return Registered;
}

}   // namespace Slate
