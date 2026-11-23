//============================================================================================================================================
//                                                         SPATIALMANIPULATOR.CPP
//============================================================================================================================================
// 🧩 `78` — the grip layout, the screen-space grasp, and the four drag solves each grip resolves against.

#include "SlateDocument/Document/SpatialManipulator/Api/SpatialManipulator.h"

#include <cmath>

namespace Slate
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE SPAN ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

// 🔴 `TransformProjection` exports compounding, projection and rebasing and nothing else — there is no normalise,
//    no cross product and no quaternion-rotates-a-span among them. The four solves below are all built from those
//    three operations, so they are written here rather than promoted: promoting them would put a general vector
//    library in `02`'s surface, and `02` §1 declares the numeric foundation to be the types and the guarantees,
//    not an arithmetic of convenience that every later component then reaches into differently.
struct DirectionSpan
{
    double SpanX = 0.0;   // [-] - document space unless the site says otherwise
    double SpanY = 0.0;   // [-]
    double SpanZ = 0.0;   // [-]
};

const double PiConstant       = 3.14159265358979323846;
const double DegreesToRadians = PiConstant / 180.0;
const double ParallelEpsilon  = 1.0e-6;   // [-] - the denominator guard `Gizmo.html`'s axis solve carries

double SpanDot(const DirectionSpan& First, const DirectionSpan& Second)
{
    return First.SpanX * Second.SpanX + First.SpanY * Second.SpanY + First.SpanZ * Second.SpanZ;
}

DirectionSpan SpanCross(const DirectionSpan& First, const DirectionSpan& Second)
{
    DirectionSpan Crossed;
    Crossed.SpanX = First.SpanY * Second.SpanZ - First.SpanZ * Second.SpanY;
    Crossed.SpanY = First.SpanZ * Second.SpanX - First.SpanX * Second.SpanZ;
    Crossed.SpanZ = First.SpanX * Second.SpanY - First.SpanY * Second.SpanX;
    return Crossed;
}

DirectionSpan SpanScaled(const DirectionSpan& Source, double Factor)
{
    DirectionSpan Scaled;
    Scaled.SpanX = Source.SpanX * Factor;
    Scaled.SpanY = Source.SpanY * Factor;
    Scaled.SpanZ = Source.SpanZ * Factor;
    return Scaled;
}

DirectionSpan SpanSum(const DirectionSpan& First, const DirectionSpan& Second)
{
    DirectionSpan Summed;
    Summed.SpanX = First.SpanX + Second.SpanX;
    Summed.SpanY = First.SpanY + Second.SpanY;
    Summed.SpanZ = First.SpanZ + Second.SpanZ;
    return Summed;
}

DirectionSpan NormaliseSpan(const DirectionSpan& Source)
{
    const double Length = std::sqrt(SpanDot(Source, Source));

    if (Length <= 0.0)
        return DirectionSpan{};

    return SpanScaled(Source, 1.0 / Length);
}

// 📐 The quaternion sandwich, expanded — the same arithmetic `46` carries for its own view direction. Deriving a
//    matrix and multiplying by it would be that arithmetic with a temporary in the middle, and the temporary is
//    what a later reader caches.
DirectionSpan RotateSpan(RotationQuaternion Rotation, const DirectionSpan& Source)
{
    DirectionSpan Imaginary;
    Imaginary.SpanX = Rotation.ImaginaryX;
    Imaginary.SpanY = Rotation.ImaginaryY;
    Imaginary.SpanZ = Rotation.ImaginaryZ;

    const DirectionSpan FirstCross  = SpanCross(Imaginary, Source);
    const DirectionSpan SecondCross = SpanCross(Imaginary, FirstCross);

    DirectionSpan Turned;
    Turned.SpanX = Source.SpanX + 2.0 * (Rotation.Real * FirstCross.SpanX + SecondCross.SpanX);
    Turned.SpanY = Source.SpanY + 2.0 * (Rotation.Real * FirstCross.SpanY + SecondCross.SpanY);
    Turned.SpanZ = Source.SpanZ + 2.0 * (Rotation.Real * FirstCross.SpanZ + SecondCross.SpanZ);
    return Turned;
}

RotationQuaternion RotationAbout(const DirectionSpan& Axis, double Radians)
{
    RotationQuaternion Turning;

    const DirectionSpan UnitAxis = NormaliseSpan(Axis);
    const double        HalfSine = std::sin(Radians * 0.5);

    Turning.ImaginaryX = UnitAxis.SpanX * HalfSine;
    Turning.ImaginaryY = UnitAxis.SpanY * HalfSine;
    Turning.ImaginaryZ = UnitAxis.SpanZ * HalfSine;
    Turning.Real       = std::cos(Radians * 0.5);
    return Turning;
}

// 📐 Shepperd's branch, over the three images of the local axes. The branch on the largest diagonal term is not an
//    optimisation — the single-branch form divides by a quantity that vanishes at a half turn, and the grip that
//    would carry it is the one drawn facing directly away from the artist.
RotationQuaternion ProjectBasisRotation(const DirectionSpan& FirstImage,
                                        const DirectionSpan& SecondImage,
                                        const DirectionSpan& ThirdImage)
{
    RotationQuaternion Turning;

    const double Trace = FirstImage.SpanX + SecondImage.SpanY + ThirdImage.SpanZ;

    if (Trace > 0.0)
    {
        const double Scale = std::sqrt(Trace + 1.0) * 2.0;
        Turning.Real       = 0.25 * Scale;
        Turning.ImaginaryX = (SecondImage.SpanZ - ThirdImage.SpanY)  / Scale;
        Turning.ImaginaryY = (ThirdImage.SpanX  - FirstImage.SpanZ)  / Scale;
        Turning.ImaginaryZ = (FirstImage.SpanY  - SecondImage.SpanX) / Scale;
        return Turning;
    }

    if (FirstImage.SpanX > SecondImage.SpanY && FirstImage.SpanX > ThirdImage.SpanZ)
    {
        const double Scale = std::sqrt(1.0 + FirstImage.SpanX - SecondImage.SpanY - ThirdImage.SpanZ) * 2.0;
        Turning.Real       = (SecondImage.SpanZ - ThirdImage.SpanY)  / Scale;
        Turning.ImaginaryX = 0.25 * Scale;
        Turning.ImaginaryY = (SecondImage.SpanX + FirstImage.SpanY)  / Scale;
        Turning.ImaginaryZ = (ThirdImage.SpanX  + FirstImage.SpanZ)  / Scale;
        return Turning;
    }

    if (SecondImage.SpanY > ThirdImage.SpanZ)
    {
        const double Scale = std::sqrt(1.0 + SecondImage.SpanY - FirstImage.SpanX - ThirdImage.SpanZ) * 2.0;
        Turning.Real       = (ThirdImage.SpanX  + FirstImage.SpanZ)  / Scale;
        Turning.ImaginaryX = (SecondImage.SpanX + FirstImage.SpanY)  / Scale;
        Turning.ImaginaryY = 0.25 * Scale;
        Turning.ImaginaryZ = (ThirdImage.SpanY  + SecondImage.SpanZ) / Scale;
        return Turning;
    }

    const double Scale = std::sqrt(1.0 + ThirdImage.SpanZ - FirstImage.SpanX - SecondImage.SpanY) * 2.0;
    Turning.Real       = (FirstImage.SpanY  - SecondImage.SpanX) / Scale;
    Turning.ImaginaryX = (ThirdImage.SpanX  + FirstImage.SpanZ)  / Scale;
    Turning.ImaginaryY = (ThirdImage.SpanY  + SecondImage.SpanZ) / Scale;
    Turning.ImaginaryZ = 0.25 * Scale;
    return Turning;
}

double Quantise(double Measured, double Increment)
{
    if (Increment <= 0.0)
        return Measured;

    return std::floor(Measured / Increment + 0.5) * Increment;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                THE MANIPULATOR BASIS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The pairing is **cyclic** — along pairs with (up, across), up with (across, along), across with (along, up) —
//    and not the fixed (first, second) pairing the reference demonstration reaches for. Both name the same plane and
//    the same bisector, so the plane grip's corner and the rotation arc's centre are identical either way; the
//    cyclic one additionally makes every pair right-handed against its own axis, and the derived rotation is then a
//    rotation for all three rather than for two of them and a reflection for the middle.
struct ManipulatorBasis
{
    DirectionSpan AxisSpan[3];        // [-] - the reference orientation's three axes, in document space
    DirectionSpan FirstPairSpan[3];   // [-] - the first span of each axis's own plane
    DirectionSpan SecondPairSpan[3];  // [-] - its second; the cross of the two is the axis itself
};

ManipulatorBasis ProjectBasis(RotationQuaternion Orientation)
{
    ManipulatorBasis Derived;

    Derived.AxisSpan[0] = RotateSpan(Orientation, DirectionSpan{ 1.0, 0.0, 0.0 });
    Derived.AxisSpan[1] = RotateSpan(Orientation, DirectionSpan{ 0.0, 1.0, 0.0 });
    Derived.AxisSpan[2] = RotateSpan(Orientation, DirectionSpan{ 0.0, 0.0, 1.0 });

    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        Derived.FirstPairSpan[Index]  = Derived.AxisSpan[(Index + 1u) % 3u];
        Derived.SecondPairSpan[Index] = Derived.AxisSpan[(Index + 2u) % 3u];
    }

    return Derived;
}

// 📝 The same pairing over the manipulator's own space, which is where the grips are declared. Written as the
//    identity orientation through the routine above rather than as a second table, so a change to the pairing is
//    a change to one place and the drawn grip cannot disagree with the axis its drag resolves against.
ManipulatorBasis LocalBasis()
{
    return ProjectBasis(RotationQuaternion{});
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE GRIP COLOURS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 Display space, not working space — `80` §2's rule for every overlay. An overlay colour carried through the
//    working space would be tone-mapped with the image at `66`, and the manipulator would then change colour as the
//    artist changed exposure on the surface behind it.
ColourSpecification DeclareOverlayColour(double RedByte, double GreenByte, double BlueByte)
{
    ColourSpecification Declaring;
    Declaring.RedCoordinate   = RedByte   / 255.0;
    Declaring.GreenCoordinate = GreenByte / 255.0;
    Declaring.BlueCoordinate  = BlueByte  / 255.0;
    Declaring.SpaceIdentity   = DisplaySpaceIdentity;
    return Declaring;
}

ColourSpecification AxisColour(std::uint32_t AxisIndex)
{
    if (AxisIndex == 0u)  return DeclareOverlayColour(224.0,  20.0,  20.0);   // [-] - the first axis
    if (AxisIndex == 1u)  return DeclareOverlayColour( 18.0, 212.0,  10.0);   // [-] - the second
    return DeclareOverlayColour(21.0, 96.0, 224.0);                            // [-] - the third
}

ColourSpecification PlaneColour(std::uint32_t AxisIndex)
{
    if (AxisIndex == 0u)  return DeclareOverlayColour( 31.0, 199.0, 199.0);   // [-] - the plane about the first axis
    if (AxisIndex == 1u)  return DeclareOverlayColour(200.0,  30.0, 200.0);   // [-] - about the second
    return DeclareOverlayColour(224.0, 205.0, 18.0);                           // [-] - about the third
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE INTERSECTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The closest approach between the ray and the capsule's own span, rather than the capsule's entry point. The
//    two order identically between any two grips, which is the whole of what `Grasp` reads the parameter for, and
//    the entry point costs a quadratic per grip on every pointer sample.
bool IntersectCapsule(const DirectionSpan& RayOrigin,
                      const DirectionSpan& RayDirection,
                      const DirectionSpan& SpanNear,
                      const DirectionSpan& SpanFar,
                      double               HalfExtent,
                      double&              RayParameter)
{
    if (HalfExtent <= 0.0)
        return false;

    DirectionSpan SegmentSpan;
    SegmentSpan.SpanX = SpanFar.SpanX - SpanNear.SpanX;
    SegmentSpan.SpanY = SpanFar.SpanY - SpanNear.SpanY;
    SegmentSpan.SpanZ = SpanFar.SpanZ - SpanNear.SpanZ;

    DirectionSpan ToNear;
    ToNear.SpanX = RayOrigin.SpanX - SpanNear.SpanX;
    ToNear.SpanY = RayOrigin.SpanY - SpanNear.SpanY;
    ToNear.SpanZ = RayOrigin.SpanZ - SpanNear.SpanZ;

    const double RayOnRay     = SpanDot(RayDirection, RayDirection);
    const double RayOnSegment = SpanDot(RayDirection, SegmentSpan);
    const double SegOnSegment = SpanDot(SegmentSpan, SegmentSpan);
    const double RayOnNear    = SpanDot(RayDirection, ToNear);
    const double SegOnNear    = SpanDot(SegmentSpan, ToNear);

    const double Denominator = RayOnRay * SegOnSegment - RayOnSegment * RayOnSegment;

    double RayDistance     = 0.0;
    double SegmentDistance = 0.0;

    if (std::fabs(Denominator) < ParallelEpsilon || SegOnSegment <= 0.0)
    {
        SegmentDistance = 0.0;
        RayDistance     = -RayOnNear / RayOnRay;
    }
    else
    {
        RayDistance     = (RayOnSegment * SegOnNear - SegOnSegment * RayOnNear) / Denominator;
        SegmentDistance = (RayOnRay     * SegOnNear - RayOnSegment * RayOnNear) / Denominator;

        if (SegmentDistance < 0.0)  SegmentDistance = 0.0;
        if (SegmentDistance > 1.0)  SegmentDistance = 1.0;

        RayDistance = (RayOnSegment * SegmentDistance - RayOnNear) / RayOnRay;
    }

    if (RayDistance < 0.0)
        RayDistance = 0.0;

    DirectionSpan Separation;
    Separation.SpanX = (RayOrigin.SpanX + RayDirection.SpanX * RayDistance)
                     - (SpanNear.SpanX  + SegmentSpan.SpanX  * SegmentDistance);
    Separation.SpanY = (RayOrigin.SpanY + RayDirection.SpanY * RayDistance)
                     - (SpanNear.SpanY  + SegmentSpan.SpanY  * SegmentDistance);
    Separation.SpanZ = (RayOrigin.SpanZ + RayDirection.SpanZ * RayDistance)
                     - (SpanNear.SpanZ  + SegmentSpan.SpanZ  * SegmentDistance);

    if (SpanDot(Separation, Separation) > HalfExtent * HalfExtent)
        return false;

    RayParameter = RayDistance;
    return true;
}

// 📐 `Gizmo.html`'s own axis solve, unchanged: the parameter along the constraint line at the line's closest
//    approach to the pointer ray. Written against the line rather than against a segment because a drag carries
//    the target past the grip it began on, and a clamped segment would stop reporting at exactly that moment.
bool SolveAxisParameter(const DirectionSpan& RayOrigin,
                        const DirectionSpan& RayDirection,
                        const DirectionSpan& LineOrigin,
                        const DirectionSpan& LineDirection,
                        double&              LineParameter)
{
    DirectionSpan ToLine;
    ToLine.SpanX = LineOrigin.SpanX - RayOrigin.SpanX;
    ToLine.SpanY = LineOrigin.SpanY - RayOrigin.SpanY;
    ToLine.SpanZ = LineOrigin.SpanZ - RayOrigin.SpanZ;

    const double LineOnLine = SpanDot(LineDirection, LineDirection);
    const double LineOnRay  = SpanDot(LineDirection, RayDirection);
    const double RayOnRay   = SpanDot(RayDirection, RayDirection);
    const double LineOnTo   = SpanDot(LineDirection, ToLine);
    const double RayOnTo    = SpanDot(RayDirection, ToLine);

    const double Denominator = LineOnLine * RayOnRay - LineOnRay * LineOnRay;

    if (std::fabs(Denominator) < ParallelEpsilon)
        return false;

    LineParameter = (LineOnRay * RayOnTo - RayOnRay * LineOnTo) / Denominator;
    return true;
}

bool SolvePlanePoint(const DirectionSpan& RayOrigin,
                     const DirectionSpan& RayDirection,
                     const DirectionSpan& PlaneOrigin,
                     const DirectionSpan& PlanePerpendicular,
                     DirectionSpan&       Met)
{
    const double Facing = SpanDot(RayDirection, PlanePerpendicular);

    if (std::fabs(Facing) < ParallelEpsilon)
        return false;

    DirectionSpan ToPlane;
    ToPlane.SpanX = PlaneOrigin.SpanX - RayOrigin.SpanX;
    ToPlane.SpanY = PlaneOrigin.SpanY - RayOrigin.SpanY;
    ToPlane.SpanZ = PlaneOrigin.SpanZ - RayOrigin.SpanZ;

    const double Advance = SpanDot(ToPlane, PlanePerpendicular) / Facing;

    if (Advance < 0.0)
        return false;

    Met.SpanX = RayOrigin.SpanX + RayDirection.SpanX * Advance;
    Met.SpanY = RayOrigin.SpanY + RayDirection.SpanY * Advance;
    Met.SpanZ = RayOrigin.SpanZ + RayDirection.SpanZ * Advance;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CONVERSIONS
//------------------------------------------------------------------------------------------------------------------------

DirectionSpan SpanOfPosition(DocumentPosition Position)
{
    DirectionSpan Carried;
    Carried.SpanX = Position.PositionX;
    Carried.SpanY = Position.PositionY;
    Carried.SpanZ = Position.PositionZ;
    return Carried;
}

DocumentPosition PositionOfSpan(const DirectionSpan& Span)
{
    DocumentPosition Carried;
    Carried.PositionX = Span.SpanX;
    Carried.PositionY = Span.SpanY;
    Carried.PositionZ = Span.SpanZ;
    return Carried;
}

DirectionSpan CarryToDocument(const DirectionSpan&  Local,
                              DocumentPosition      Origin,
                              RotationQuaternion    Orientation,
                              double                UnitExtent)
{
    const DirectionSpan Turned = RotateSpan(Orientation, SpanScaled(Local, UnitExtent));
    return SpanSum(SpanOfPosition(Origin), Turned);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE GRIP SOLIDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A grip's drawn solid and an authored primitive are one generation, so a modelled cone and a translate cone are
//    the same triangles under the same parameters. `78` §4 asks for the manipulator to be drawn in `80`'s recording
//    and this is what it hands over — a specification and a placement, not a second geometry.
ManipulationGrip DeclareConeGrip(std::uint32_t        AxisIndex,
                                 const DirectionSpan& AxisLocal,
                                 const DirectionSpan& FirstPair,
                                 const DirectionSpan& SecondPair)
{
    ManipulationGrip Declaring;

    Declaring.Edits      = ManipulationSubject::Translate;
    Declaring.Addressed  = static_cast<ManipulationAxis>(AxisIndex);
    Declaring.GripColour = AxisColour(AxisIndex);

    Declaring.Generated.Generated        = PrimitiveSubject::Cone;
    Declaring.Generated.HalfExtentX  = GripConeRadius;
    Declaring.Generated.HalfExtentUp     = GripConeLength * 0.5;
    Declaring.Generated.HalfExtentY = GripConeRadius;
    Declaring.Generated.RadialCount      = 24u;
    Declaring.Generated.AxialCount       = 1u;

    // 📝 The solid revolves about its own second axis, so the placement carries that axis onto the addressed one.
    //    The third image is negated because a rotation carrying the second axis onto the first pair's cross would
    //    otherwise be a reflection, and a reflected cone is a cone whose winding faces inward.
    Declaring.GripPlacement.Translation = PositionOfSpan(SpanScaled(AxisLocal, GripTipReach - GripConeLength * 0.5));
    Declaring.GripPlacement.Rotation    = ProjectBasisRotation(FirstPair, AxisLocal, SpanScaled(SecondPair, -1.0));

    Declaring.NearPosition = PositionOfSpan(SpanScaled(AxisLocal, GripTipReach - GripConeLength));
    Declaring.FarPosition  = PositionOfSpan(SpanScaled(AxisLocal, GripTipReach));
    Declaring.HalfExtent   = GripConeRadius;
    Declaring.GripDeclared = true;
    return Declaring;
}

ManipulationGrip DeclareScaleGrip(std::uint32_t        AxisIndex,
                                  const DirectionSpan& AxisLocal,
                                  const DirectionSpan& FirstPair,
                                  const DirectionSpan& SecondPair)
{
    ManipulationGrip Declaring;

    Declaring.Edits      = ManipulationSubject::Scale;
    Declaring.Addressed  = static_cast<ManipulationAxis>(AxisIndex);
    Declaring.GripColour = AxisColour(AxisIndex);

    Declaring.Generated.Generated        = PrimitiveSubject::Cylinder;
    Declaring.Generated.HalfExtentX  = GripConeRadius;
    Declaring.Generated.HalfExtentUp     = GripScaleLength * 0.5;
    Declaring.Generated.HalfExtentY = GripConeRadius;
    Declaring.Generated.RadialCount      = 24u;
    Declaring.Generated.AxialCount       = 1u;

    const double GripCentre = GripTipReach - GripScaleInboard;

    Declaring.GripPlacement.Translation = PositionOfSpan(SpanScaled(AxisLocal, GripCentre));
    Declaring.GripPlacement.Rotation    = ProjectBasisRotation(FirstPair, AxisLocal, SpanScaled(SecondPair, -1.0));

    Declaring.NearPosition = PositionOfSpan(SpanScaled(AxisLocal, GripCentre - GripScaleLength * 0.5));
    Declaring.FarPosition  = PositionOfSpan(SpanScaled(AxisLocal, GripCentre + GripScaleLength * 0.5));
    Declaring.HalfExtent   = GripConeRadius;
    Declaring.GripDeclared = true;
    return Declaring;
}

ManipulationGrip DeclarePlaneGrip(std::uint32_t        AxisIndex,
                                  const DirectionSpan& AxisLocal,
                                  const DirectionSpan& FirstPair,
                                  const DirectionSpan& SecondPair)
{
    ManipulationGrip Declaring;

    Declaring.Edits      = ManipulationSubject::PlaneTranslate;
    Declaring.Addressed  = static_cast<ManipulationAxis>(AxisIndex);
    Declaring.GripColour = PlaneColour(AxisIndex);

    Declaring.Generated.Generated        = PrimitiveSubject::Plane;
    Declaring.Generated.HalfExtentX  = GripPlaneHalfExtent;
    Declaring.Generated.HalfExtentUp     = GripPlaneHalfExtent;
    Declaring.Generated.HalfExtentY = GripPlaneHalfExtent;
    Declaring.Generated.RadialCount      = 3u;
    Declaring.Generated.AxialCount       = 1u;

    // 📝 The generated plane already lies in the first and third axes with its perpendicular on the second, so the
    //    placement is one rotation and not the two the reference demonstration needs — that one lays a quad out of
    //    the display plane first and then turns it, because its own plane is declared facing the viewer.
    const DirectionSpan Corner = SpanScaled(SpanSum(FirstPair, SecondPair), GripTipReach - GripPlaneHalfExtent);

    Declaring.GripPlacement.Translation = PositionOfSpan(Corner);
    Declaring.GripPlacement.Rotation    = ProjectBasisRotation(FirstPair, AxisLocal, SpanScaled(SecondPair, -1.0));

    // 📝 One sphere at the corner rather than a capsule across the quad. Its radius is the quad's own half-extent,
    //    so the grasped extent is the disc inscribed in the quad — the four corners outside it are the pixels
    //    nearest the two axis grips, and yielding them to the smaller target is what makes the smaller target
    //    reachable at all.
    Declaring.NearPosition = PositionOfSpan(Corner);
    Declaring.FarPosition  = PositionOfSpan(Corner);
    Declaring.HalfExtent   = GripPlaneHalfExtent;
    Declaring.GripDeclared = true;
    return Declaring;
}

ManipulationGrip DeclareRotationGrip(std::uint32_t        AxisIndex,
                                     const DirectionSpan& AxisLocal,
                                     const DirectionSpan& FirstPair,
                                     const DirectionSpan& SecondPair)
{
    ManipulationGrip Declaring;

    Declaring.Edits      = ManipulationSubject::Rotate;
    Declaring.Addressed  = static_cast<ManipulationAxis>(AxisIndex);
    Declaring.GripColour = AxisColour(AxisIndex);

    Declaring.Generated.Generated        = PrimitiveSubject::AnnularSector;
    Declaring.Generated.HalfExtentX  = GripArcRadius;
    Declaring.Generated.HalfExtentUp     = GripArcBand;
    Declaring.Generated.HalfExtentY = GripArcRadius;
    Declaring.Generated.MinorRadius      = GripArcBand;
    Declaring.Generated.SweepRadians     = GripArcSweep;
    Declaring.Generated.SweepOffset      = PiConstant * 0.25 - GripArcSweep * 0.5;
    Declaring.Generated.RadialCount      = 24u;
    Declaring.Generated.AxialCount       = 1u;

    // 📝 The sector sweeps from its own first axis toward its third, so the placement carries those onto the pair
    //    and the second axis onto the negated addressed axis — which is the rotation that makes the sweep run from
    //    the first span to the second rather than away from it.
    Declaring.GripPlacement.Rotation = ProjectBasisRotation(FirstPair, SpanScaled(AxisLocal, -1.0), SecondPair);

    // 📐 The capsule spans the band's chord and its half-extent is the band's own half-width plus the sagitta the
    //    chord cuts off, so the whole drawn band is inside it and nothing beyond the band's ends is. A sphere at
    //    the bisector would have had to be as large as the half-chord, and would then cover the two cones the arc
    //    sits between — which are the two grips the arc is drawn to be distinguishable from.
    const double HalfSweep   = GripArcSweep * 0.5;
    const double ChordCosine = std::cos(HalfSweep);
    const double ChordSine   = std::sin(HalfSweep);
    const double Sagitta     = GripArcRadius * (1.0 - ChordCosine);

    const DirectionSpan Bisector    = NormaliseSpan(SpanSum(FirstPair, SecondPair));
    const DirectionSpan Transverse  = NormaliseSpan(SpanCross(SpanScaled(AxisLocal, -1.0), Bisector));
    const DirectionSpan ChordCentre = SpanScaled(Bisector, GripArcRadius * ChordCosine);
    const DirectionSpan ChordReach  = SpanScaled(Transverse, GripArcRadius * ChordSine);

    Declaring.NearPosition = PositionOfSpan(SpanSum(ChordCentre, SpanScaled(ChordReach, -1.0)));
    Declaring.FarPosition  = PositionOfSpan(SpanSum(ChordCentre, ChordReach));
    Declaring.HalfExtent   = GripArcBand + Sagitta;
    Declaring.GripDeclared = true;
    return Declaring;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE LAYOUT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ManipulationLayout::Layout(DocumentPosition        Origin,
                                         RotationQuaternion      Orientation,
                                         const CameraProjection& Camera,
                                         ManipulatedSubject      Addressing)
{
    if (Addressing == ManipulatedSubject::TargetCount)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the closed target count names no target to manipulate" });
    }

    if (Addressing == ManipulatedSubject::Nothing)
    {
        Reclaim();
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "no target is addressed; the manipulator is not presented" });
    }

    if (Camera.DerivationOwed())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the camera owes a reconciliation; its projection is last tick's" });
    }

    const CameraSpecification& Declaring = Camera.Declared();

    // 📐 The view height at the layout's own distance, and the axis length as a declared fraction of it. `46`'s
    //    extent parameter is the field **across** the display in degrees, so the height is twice the distance times
    //    the tangent of its half — and the parallel projection's is the parameter itself, at every distance, which
    //    is the whole difference between the two and the reason this is not one expression.
    double ViewHeight = 0.0;

    if (Declaring.Projected == ProjectionSubject::Parallel)
    {
        ViewHeight = Declaring.ExtentParameter;
    }
    else
    {
        DirectionSpan ToCamera;
        ToCamera.SpanX = Declaring.Placement.Translation.PositionX - Origin.PositionX;
        ToCamera.SpanY = Declaring.Placement.Translation.PositionY - Origin.PositionY;
        ToCamera.SpanZ = Declaring.Placement.Translation.PositionZ - Origin.PositionZ;

        const double Distance  = std::sqrt(SpanDot(ToCamera, ToCamera));
        const double HalfAngle = Declaring.ExtentParameter * 0.5 * DegreesToRadians;

        ViewHeight = 2.0 * Distance * std::tan(HalfAngle);
    }

    const double LaidExtent = ViewHeight * GripViewFraction;

    if (!(LaidExtent > 0.0))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the camera resolves no view height at the layout's own position" });
    }

    // 🔴 A camera has no scale to edit — `46` declares a placement, a projection and an exposure, and none of the
    //    three is a scale. The rule lives here rather than in the caller because a caller deciding it would be a
    //    second place it is declared, and the two would disagree the first time either was amended.
    const bool ScaleOffered = Addressing != ManipulatedSubject::OneCamera;

    const ManipulatorBasis Local = LocalBasis();

    Declared.clear();
    Declared.reserve(GripLimit);

    // 📝 Grasp precedence is the declaration order — cones, then scale grips, then planes, then arcs, then the
    //    ring. `78` §4 gives ties to the lower ordinal, so a pointer over the overlap of a cone and an arc grasps
    //    the cone, which is the smaller target and therefore the one that was aimed at.
    for (std::uint32_t AxisIndex = 0u; AxisIndex < 3u; ++AxisIndex)
    {
        Declared.push_back(DeclareConeGrip(AxisIndex,
                                           Local.AxisSpan[AxisIndex],
                                           Local.FirstPairSpan[AxisIndex],
                                           Local.SecondPairSpan[AxisIndex]));
    }

    if (ScaleOffered)
    {
        for (std::uint32_t AxisIndex = 0u; AxisIndex < 3u; ++AxisIndex)
        {
            Declared.push_back(DeclareScaleGrip(AxisIndex,
                                                Local.AxisSpan[AxisIndex],
                                                Local.FirstPairSpan[AxisIndex],
                                                Local.SecondPairSpan[AxisIndex]));
        }
    }

    for (std::uint32_t AxisIndex = 0u; AxisIndex < 3u; ++AxisIndex)
    {
        Declared.push_back(DeclarePlaneGrip(AxisIndex,
                                            Local.AxisSpan[AxisIndex],
                                            Local.FirstPairSpan[AxisIndex],
                                            Local.SecondPairSpan[AxisIndex]));
    }

    for (std::uint32_t AxisIndex = 0u; AxisIndex < 3u; ++AxisIndex)
    {
        Declared.push_back(DeclareRotationGrip(AxisIndex,
                                               Local.AxisSpan[AxisIndex],
                                               Local.FirstPairSpan[AxisIndex],
                                               Local.SecondPairSpan[AxisIndex]));
    }

    // 🔴 The central ring is drawn and is **not** grasped — its half-extent is zero, and `Grasp` never reports a
    //    grip it cannot intersect. It marks the manipulator's own origin, which is the one thing about the
    //    manipulator that no other grip states: with every grip pointing away from it, the origin the scale and
    //    rotation are about would otherwise be inferred from where the arcs happen to meet.
    {
        ManipulationGrip Ring;

        Ring.Edits      = ManipulationSubject::Rotate;
        Ring.Addressed  = ManipulationAxis::AxisScreen;
        Ring.GripColour = DeclareOverlayColour(255.0, 255.0, 255.0);

        Ring.Generated.Generated        = PrimitiveSubject::Torus;
        Ring.Generated.HalfExtentX  = GripRingRadius;
        Ring.Generated.HalfExtentUp     = GripRingBand;
        Ring.Generated.HalfExtentY = GripRingRadius;
        Ring.Generated.MinorRadius      = GripRingBand;
        Ring.Generated.RadialCount      = 48u;
        Ring.Generated.AxialCount       = 12u;

        // 📝 The ring faces the display and not the artist. The camera's own rotation is copied rather than the
        //    span toward the camera being derived, so the ring stays a circle across the whole display instead of
        //    turning into an ellipse away from its centre — the same convention the reference demonstration keeps.
        const DirectionSpan CameraFirst = RotateSpan(Declaring.Placement.Rotation, DirectionSpan{ 1.0, 0.0, 0.0 });
        const DirectionSpan CameraUp    = RotateSpan(Declaring.Placement.Rotation, DirectionSpan{ 0.0, 1.0, 0.0 });
        const DirectionSpan CameraOut   = RotateSpan(Declaring.Placement.Rotation, DirectionSpan{ 0.0, 0.0, 1.0 });

        // 📝 Held in the manipulator's own space, so the ring is billboarded once here rather than by whoever
        //    records it. The reference orientation is conjugated out by rotating the camera's axes back through it.
        RotationQuaternion Conjugated = Orientation;
        Conjugated.ImaginaryX = -Conjugated.ImaginaryX;
        Conjugated.ImaginaryY = -Conjugated.ImaginaryY;
        Conjugated.ImaginaryZ = -Conjugated.ImaginaryZ;

        Ring.GripPlacement.Rotation = ProjectBasisRotation(RotateSpan(Conjugated, CameraFirst),
                                                           RotateSpan(Conjugated, CameraOut),
                                                           RotateSpan(Conjugated, SpanScaled(CameraUp, -1.0)));

        Ring.HalfExtent   = 0.0;
        Ring.GripDeclared = true;

        Declared.push_back(Ring);
    }

    LaidOrigin      = Origin;
    LaidOrientation = Orientation;
    LaidTarget      = Addressing;
    LaidUnitExtent  = LaidExtent;
    LayoutDeclared  = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE GRASP
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> ManipulationLayout::Grasp(const CameraProjection& Camera,
                                                 double                  PointerX,
                                                 double                  PointerY,
                                                 std::uint32_t           DisplayX,
                                                 std::uint32_t           DisplayY) const
{
    if (!LayoutDeclared)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "no layout stands; there is nothing to grasp" });
    }

    const Deliver<ProjectedRay> Cast = ProjectPointerRay(Camera, PointerX, PointerY,
                                                         DisplayX, DisplayY);

    if (!Cast.Resolved)
    {
        return Deliver<std::uint32_t>::Refuse(Cast.Error);
    }

    const ProjectedRay& Pointing = Cast.Resolve();

    const DirectionSpan RayOrigin    = SpanOfPosition(Pointing.Origin);
    const DirectionSpan RayDirection = { Pointing.DirectionX, Pointing.DirectionY, Pointing.DirectionZ };

    std::uint32_t GraspedIndex = 0u;
    double        NearestReach   = 0.0;
    bool          GraspDeclared  = false;

    for (std::uint32_t Index = 0u; Index < static_cast<std::uint32_t>(Declared.size()); ++Index)
    {
        const ManipulationGrip& Testing = Declared[Index];

        if (!Testing.GripDeclared)
            continue;

        const DirectionSpan SpanNear = CarryToDocument(SpanOfPosition(Testing.NearPosition),
                                                       LaidOrigin, LaidOrientation, LaidUnitExtent);
        const DirectionSpan SpanFar  = CarryToDocument(SpanOfPosition(Testing.FarPosition),
                                                       LaidOrigin, LaidOrientation, LaidUnitExtent);

        double Reach = 0.0;

        if (!IntersectCapsule(RayOrigin, RayDirection, SpanNear, SpanFar,
                              Testing.HalfExtent * LaidUnitExtent, Reach))
        {
            continue;
        }

        // 📝 Strictly nearer, so a tie falls to the grip already held — which is the lower ordinal, because the
        //    walk is in declaration order and `78` §4 declares that order to be the precedence.
        if (!GraspDeclared || Reach < NearestReach)
        {
            GraspedIndex = Index;
            NearestReach   = Reach;
            GraspDeclared  = true;
        }
    }

    if (!GraspDeclared)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "the pointer grasps no grip of the standing layout" });
    }

    return Deliver<std::uint32_t>::Result(GraspedIndex);
}

Deliver<const ManipulationGrip*> ManipulationLayout::Resolve(std::uint32_t GripIndex) const
{
    if (GripIndex >= static_cast<std::uint32_t>(Declared.size()))
    {
        return Deliver<const ManipulationGrip*>::Refuse(
            { RefusalReason::ContentUnsupported, "the ordinal is outside the standing layout" });
    }

    if (!Declared[GripIndex].GripDeclared)
    {
        return Deliver<const ManipulationGrip*>::Refuse(
            { RefusalReason::ContentUnsupported, "this target offers no grip at that ordinal" });
    }

    return Deliver<const ManipulationGrip*>::Result(&Declared[GripIndex]);
}

const std::vector<ManipulationGrip>& ManipulationLayout::Grips() const
{
    return Declared;
}

DocumentPosition ManipulationLayout::Origin() const
{
    return LaidOrigin;
}

RotationQuaternion ManipulationLayout::Orientation() const
{
    return LaidOrientation;
}

double ManipulationLayout::UnitExtent() const
{
    return LaidUnitExtent;
}

bool ManipulationLayout::LayoutCurrent() const
{
    return LayoutDeclared;
}

void ManipulationLayout::Reclaim()
{
    Declared.clear();
    LaidOrigin      = {};
    LaidOrientation = {};
    LaidTarget      = ManipulatedSubject::Nothing;
    LaidUnitExtent  = 1.0;
    LayoutDeclared  = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       OPENING A DRAG
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ManipulationSequence::Open(const ManipulationGrip&   Grasping,
                                         const ManipulationLayout& Laid,
                                         const CameraProjection&   Camera,
                                         double                    PointerX,
                                         double                    PointerY,
                                         std::uint32_t             DisplayX,
                                         std::uint32_t             DisplayY)
{
    if (OpenDeclared)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "a manipulation is already open; seal or abandon it first" });
    }

    if (!Grasping.GripDeclared || !Laid.LayoutCurrent())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the grip is undeclared, or no layout stands behind it" });
    }

    const Deliver<ProjectedRay> Cast = ProjectPointerRay(Camera, PointerX, PointerY,
                                                         DisplayX, DisplayY);

    if (!Cast.Resolved)
    {
        return Deliver<bool>::Refuse(Cast.Error);
    }

    const ProjectedRay& Pointing = Cast.Resolve();

    const DirectionSpan RayOrigin    = SpanOfPosition(Pointing.Origin);
    const DirectionSpan RayDirection = { Pointing.DirectionX, Pointing.DirectionY, Pointing.DirectionZ };

    const ManipulatorBasis Basis        = ProjectBasis(Laid.Orientation());
    const DirectionSpan    OriginSpan   = SpanOfPosition(Laid.Origin());
    const std::uint32_t    AxisIndex  = static_cast<std::uint32_t>(Grasping.Addressed);

    if (AxisIndex >= 3u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the grip addresses no axis of the reference orientation" });
    }

    const DirectionSpan AxisSpan   = Basis.AxisSpan[AxisIndex];
    const DirectionSpan FirstSpan  = Basis.FirstPairSpan[AxisIndex];
    const DirectionSpan SecondSpan = Basis.SecondPairSpan[AxisIndex];

    if (Grasping.Edits == ManipulationSubject::PlaneTranslate)
    {
        DirectionSpan Met;

        if (!SolvePlanePoint(RayOrigin, RayDirection, OriginSpan, AxisSpan, Met))
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the pointer resolves no position on the grip's own plane" });
        }

        FixedConstraint = ConstraintSubject::PlaneConstrained;
        OpenPlanePoint  = PositionOfSpan(Met);
    }
    else if (Grasping.Edits == ManipulationSubject::Rotate)
    {
        DirectionSpan Met;

        if (!SolvePlanePoint(RayOrigin, RayDirection, OriginSpan, AxisSpan, Met))
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the pointer resolves no position on the rotation's own plane" });
        }

        DirectionSpan FromOrigin;
        FromOrigin.SpanX = Met.SpanX - OriginSpan.SpanX;
        FromOrigin.SpanY = Met.SpanY - OriginSpan.SpanY;
        FromOrigin.SpanZ = Met.SpanZ - OriginSpan.SpanZ;

        const DirectionSpan Transverse = SpanCross(AxisSpan, FirstSpan);

        FixedConstraint = ConstraintSubject::AxisConstrained;
        OpenAngle       = std::atan2(SpanDot(FromOrigin, Transverse), SpanDot(FromOrigin, FirstSpan));
        OpenPlanePoint  = PositionOfSpan(Met);
    }
    else
    {
        double Parameter = 0.0;

        if (!SolveAxisParameter(RayOrigin, RayDirection, OriginSpan, AxisSpan, Parameter))
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the pointer ray lies along the constraint axis" });
        }

        FixedConstraint = ConstraintSubject::AxisConstrained;
        OpenParameter   = Parameter;
    }

    GraspedGrip     = Grasping;
    Current        = {};
    Current.Edited = Grasping.Edits;

    HeldCamera      = Camera;
    DragOrigin      = Laid.Origin();
    UnitExtent      = Laid.UnitExtent();

    AxisXSpan   = AxisSpan.SpanX;
    AxisUpSpan      = AxisSpan.SpanY;
    AxisYSpan  = AxisSpan.SpanZ;

    PlaneXSpan  = FirstSpan.SpanX;
    PlaneUpSpan     = FirstSpan.SpanY;
    PlaneYSpan = FirstSpan.SpanZ;

    ReferenceX  = SecondSpan.SpanX;
    ReferenceUp     = SecondSpan.SpanY;
    ReferenceY = SecondSpan.SpanZ;

    OpenDeclared    = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     AMENDING A DRAG
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ManipulationSequence::Amend(double        PointerX,
                                          double        PointerY,
                                          std::uint32_t DisplayX,
                                          std::uint32_t DisplayY,
                                          bool          SnapDeclared)
{
    if (!OpenDeclared)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "no manipulation is open to amend" });
    }

    // 🔴 The camera read at Open, not the one standing now. An artist who orbits mid-drag moves the display and
    //    not the plane the drag resolves against, which is `78` §2's whole rule — re-reading here would make the
    //    manipulated object jump by whatever the orbit changed, at the moment the artist was doing something else.
    const Deliver<ProjectedRay> Cast = ProjectPointerRay(HeldCamera, PointerX, PointerY,
                                                         DisplayX, DisplayY);

    if (!Cast.Resolved)
    {
        return Deliver<bool>::Refuse(Cast.Error);
    }

    const ProjectedRay& Pointing = Cast.Resolve();

    const DirectionSpan RayOrigin    = SpanOfPosition(Pointing.Origin);
    const DirectionSpan RayDirection = { Pointing.DirectionX, Pointing.DirectionY, Pointing.DirectionZ };
    const DirectionSpan OriginSpan   = SpanOfPosition(DragOrigin);

    const DirectionSpan AxisSpan   = { AxisXSpan,  AxisUpSpan,  AxisYSpan  };
    const DirectionSpan FirstSpan  = { PlaneXSpan, PlaneUpSpan, PlaneYSpan };
    const DirectionSpan SecondSpan = { ReferenceX, ReferenceUp, ReferenceY };

    ManipulationAmendment Amending;
    Amending.Edited = GraspedGrip.Edits;

    if (GraspedGrip.Edits == ManipulationSubject::PlaneTranslate)
    {
        DirectionSpan Met;

        if (!SolvePlanePoint(RayOrigin, RayDirection, OriginSpan, AxisSpan, Met))
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the pointer resolves no position on the fixed plane" });
        }

        DirectionSpan Moved;
        Moved.SpanX = Met.SpanX - OpenPlanePoint.PositionX;
        Moved.SpanY = Met.SpanY - OpenPlanePoint.PositionY;
        Moved.SpanZ = Met.SpanZ - OpenPlanePoint.PositionZ;

        // 📝 Snapped per span of the plane and not on the resulting position, so a plane drag lands on the same
        //    lattice an axis drag along either of its two spans would have landed on.
        double FirstReach  = SpanDot(Moved, FirstSpan);
        double SecondReach = SpanDot(Moved, SecondSpan);

        if (SnapDeclared)
        {
            FirstReach  = Quantise(FirstReach,  SnapTranslation);
            SecondReach = Quantise(SecondReach, SnapTranslation);
        }

        const DirectionSpan Displaced = SpanSum(SpanScaled(FirstSpan,  FirstReach),
                                                SpanScaled(SecondSpan, SecondReach));

        Amending.Displacement = PositionOfSpan(Displaced);
    }
    else if (GraspedGrip.Edits == ManipulationSubject::Rotate)
    {
        DirectionSpan Met;

        if (!SolvePlanePoint(RayOrigin, RayDirection, OriginSpan, AxisSpan, Met))
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the pointer resolves no position on the fixed rotation plane" });
        }

        DirectionSpan FromOrigin;
        FromOrigin.SpanX = Met.SpanX - OriginSpan.SpanX;
        FromOrigin.SpanY = Met.SpanY - OriginSpan.SpanY;
        FromOrigin.SpanZ = Met.SpanZ - OriginSpan.SpanZ;

        const DirectionSpan Transverse  = SpanCross(AxisSpan, FirstSpan);
        const double        CurrentArc = std::atan2(SpanDot(FromOrigin, Transverse),
                                                     SpanDot(FromOrigin, FirstSpan));

        double Turned = CurrentArc - OpenAngle;

        if (SnapDeclared)
            Turned = Quantise(Turned, SnapRotationStep);

        Amending.Turned        = RotationAbout(AxisSpan, Turned);
        Amending.TurnedRadians = Turned;
    }
    else
    {
        double Parameter = 0.0;

        if (!SolveAxisParameter(RayOrigin, RayDirection, OriginSpan, AxisSpan, Parameter))
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the pointer ray lies along the fixed axis" });
        }

        const double Reach = Parameter - OpenParameter;

        if (GraspedGrip.Edits == ManipulationSubject::Scale)
        {
            // 📐 The reach is divided by the manipulator unit, so dragging the scale grip out by one axis length
            //    doubles the factor at every camera distance. Taken as a document distance instead, the same
            //    gesture would scale by a millimetre count and would therefore mean something different every time
            //    the artist moved the camera — which is exactly what the constant display extent exists to prevent.
            double Factor = 1.0 + Reach / UnitExtent;

            if (SnapDeclared)
            {
                Factor = Quantise(Factor, SnapScaleStep);

                if (Factor < SnapScaleStep)
                    Factor = SnapScaleStep;
            }

            // 🔴 Clamped above zero rather than allowed through it. A factor dragged through zero passes through a
            //    surface with no extent and comes out inverted, and the inversion is not visible as an inversion —
            //    it reads as the surface having turned inside out for no reason the artist can undo by dragging back.
            if (Factor < ScaleFactorMinimum)
                Factor = ScaleFactorMinimum;

            const std::uint32_t AxisIndex = static_cast<std::uint32_t>(GraspedGrip.Addressed);

            if (AxisIndex == 0u)  Amending.ScaleX  = Factor;
            if (AxisIndex == 1u)  Amending.ScaleUp     = Factor;
            if (AxisIndex == 2u)  Amending.ScaleY = Factor;
        }
        else
        {
            double Displaced = Reach;

            if (SnapDeclared)
                Displaced = Quantise(Displaced, SnapTranslation);

            Amending.Displacement = PositionOfSpan(SpanScaled(AxisSpan, Displaced));
        }
    }

    Current = Amending;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENDING A DRAG
//------------------------------------------------------------------------------------------------------------------------

Deliver<ManipulationAmendment> ManipulationSequence::Abandon()
{
    if (!OpenDeclared)
    {
        return Deliver<ManipulationAmendment>::Refuse(
            { RefusalReason::HostDenied, "no manipulation is open to abandon" });
    }

    // 🔴 The **identity** amendment is delivered and not the one the drag had reached. A caller that applies what
    //    it is handed applies nothing, which is what abandoning means; handing back the standing amendment would
    //    make abandoning and sealing differ only in what the caller remembered to do with the result.
    ManipulationAmendment Abandoned;
    Abandoned.Edited = GraspedGrip.Edits;

    GraspedGrip  = {};
    Current     = {};
    OpenDeclared = false;

    return Deliver<ManipulationAmendment>::Result(Abandoned);
}

Deliver<ManipulationAmendment> ManipulationSequence::Seal()
{
    if (!OpenDeclared)
    {
        return Deliver<ManipulationAmendment>::Refuse(
            { RefusalReason::HostDenied, "no manipulation is open to seal" });
    }

    const ManipulationAmendment Sealed = Current;

    GraspedGrip  = {};
    Current     = {};
    OpenDeclared = false;

    return Deliver<ManipulationAmendment>::Result(Sealed);
}

const ManipulationAmendment& ManipulationSequence::Amended() const
{
    return Current;
}

bool ManipulationSequence::DragOpen() const
{
    return OpenDeclared;
}

ConstraintSubject ManipulationSequence::Constrained() const
{
    return FixedConstraint;
}

const ManipulationGrip& ManipulationSequence::Grasped() const
{
    return GraspedGrip;
}

}   // namespace Slate
