//============================================================================================================================================
//                                                           CAMERAPROJECTION.CPP
//============================================================================================================================================
// 🧩 Reversed-depth projection derivation, plane extraction, the gesture lifecycle, and the framing solve.

#include "SlateDocument/Document/CameraProjection/Api/CameraProjection.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATRIX HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Column-major throughout, matching `ProjectedTransform`. The coefficient at row r of column c is at c*4 + r,
//    which is stated here once so that no derivation below has to restate it.
ProjectedTransform Multiply(const ProjectedTransform& Outer, const ProjectedTransform& Inner)
{
    ProjectedTransform Composed;

    for (std::uint32_t Column = 0u; Column < 4u; ++Column)
    {
        for (std::uint32_t Row = 0u; Row < 4u; ++Row)
        {
            double Accumulated = 0.0;

            for (std::uint32_t Passed = 0u; Passed < 4u; ++Passed)
                Accumulated += Outer.Coefficient[Passed * 4u + Row] * Inner.Coefficient[Column * 4u + Passed];

            Composed.Coefficient[Column * 4u + Row] = Accumulated;
        }
    }

    return Composed;
}

RotationQuaternion Conjugated(RotationQuaternion Subject)
{
    RotationQuaternion Reversed;
    Reversed.ImaginaryX = -Subject.ImaginaryX;
    Reversed.ImaginaryY = -Subject.ImaginaryY;
    Reversed.ImaginaryZ = -Subject.ImaginaryZ;
    Reversed.Real       =  Subject.Real;

    return Reversed;
}

// 📐 The quaternion sandwich, expanded. Deriving a matrix and multiplying by it would be the same arithmetic with
//    a temporary in the middle, and the temporary is what a later reader caches.
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

RotationQuaternion RotationAbout(double AxisX, double AxisY, double AxisZ, double Radians)
{
    RotationQuaternion Turning;

    const double Length = std::sqrt(AxisX * AxisX + AxisY * AxisY + AxisZ * AxisZ);

    if (Length <= 0.0)
        return Turning;

    const double Half   = Radians * 0.5;
    const double Sine   = std::sin(Half) / Length;

    Turning.ImaginaryX = AxisX * Sine;
    Turning.ImaginaryY = AxisY * Sine;
    Turning.ImaginaryZ = AxisZ * Sine;
    Turning.Real       = std::cos(Half);

    return Turning;
}

// 📝 The camera looks along its own negative third axis, which is the convention `Frame` and `NavigationSequence`
//    both read. Declared once here so the two cannot drift apart.
void ViewDirection(RotationQuaternion Rotation, double& OutX, double& OutY, double& OutZ)
{
    RotateSpan(Rotation, 0.0, 0.0, -1.0, OutX, OutY, OutZ);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE VIEW PROJECTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<ViewProjection> Derive(const CameraSpecification& Declaring)
{
    if (!Declaring.Clipping.IntervalValid())
    {
        return Deliver<ViewProjection>::Refuse(
            { RefusalReason::ContentUnsupported, "the clipping interval has no interior" });
    }

    if (Declaring.SensorProportion <= 0.0)
    {
        return Deliver<ViewProjection>::Refuse(
            { RefusalReason::ContentUnsupported, "the sensor proportion is not positive" });
    }

    if (Declaring.ExtentParameter <= 0.0)
    {
        return Deliver<ViewProjection>::Refuse(
            { RefusalReason::ContentUnsupported, "the projection's extent parameter has no interior" });
    }

    ViewProjection Derived;
    Derived.ViewOrigin = Declaring.Placement.Translation;

    // 📝 The view rotation is the placement's rotation conjugated, with no translation and no scale. `02` §3.2's
    //    rebasing subtraction supplies the translation at 64-bit, and a camera is never scaled.
    DecomposedTransform Rotating;
    Rotating.Rotation = Conjugated(Declaring.Placement.Rotation);

    Derived.ViewRotation = Project(Rotating);

    const double Nearest  = Declaring.Clipping.Nearest;
    const double Furthest = Declaring.Clipping.Furthest;
    const double Interval = Furthest - Nearest;

    ProjectedTransform Projecting;

    for (std::uint32_t Index = 0u; Index < 16u; ++Index)
        Projecting.Coefficient[Index] = 0.0;

    if (Declaring.Projected == ProjectionSubject::Perspective)
    {
        // 📐 Solving the reversed arrangement directly: with w = −z, the depth row is (A·z + B)/(−z), and requiring
        //    NearPlaneDepth at z = −Nearest and FarPlaneDepth at z = −Furthest gives A = Nearest/Interval and
        //    B = Nearest·Furthest/Interval. Deriving the forward arrangement and negating it afterwards gives the
        //    same two numbers with one more place to lose a sign.
        const double HalfAngle = Declaring.ExtentParameter * 0.5 * Pi / 180.0;
        const double Cotangent = 1.0 / std::tan(HalfAngle);

        if (!(Cotangent > 0.0))
        {
            return Deliver<ViewProjection>::Refuse(
                { RefusalReason::ContentUnsupported, "the angular field does not resolve a projection" });
        }

        Projecting.Coefficient[0]  = Cotangent / Declaring.SensorProportion;
        Projecting.Coefficient[5]  = Cotangent * ClipCoordinateSignum;
        Projecting.Coefficient[10] = Nearest / Interval;
        Projecting.Coefficient[11] = -1.0;
        Projecting.Coefficient[14] = Nearest * Furthest / Interval;
    }
    else
    {
        // 📐 The parallel arrangement is the same two conditions over a depth that is linear in z rather than in
        //    its reciprocal: a = 1/Interval and b = Furthest/Interval.
        const double HalfY = Declaring.ExtentParameter * 0.5;
        const double HalfX  = HalfY * Declaring.SensorProportion;

        Projecting.Coefficient[0]  = 1.0 / HalfX;
        Projecting.Coefficient[5]  = ClipCoordinateSignum / HalfY;
        Projecting.Coefficient[10] = 1.0 / Interval;
        Projecting.Coefficient[14] = Furthest / Interval;
        Projecting.Coefficient[15] = 1.0;
    }

    Derived.Projected = Projecting;
    Derived.Composed  = Multiply(Projecting, Derived.ViewRotation);

    return Deliver<ViewProjection>::Result(Derived);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PLANE EXTRACTION
//------------------------------------------------------------------------------------------------------------------------

void FrustumSpace::DeriveFrustumPlanes(const ViewProjection& Projected)
{
    RebasingOrigin = Projected.ViewOrigin;

    const double* Held = Projected.Composed.Coefficient;

    // 📐 Row r of a column-major matrix is (C[r], C[4+r], C[8+r], C[12+r]). Each clip-space inequality is a sum or
    //    difference of two such rows, and the four coefficients of that sum are the plane.
    const double Rows[4][4] =
    {
        { Held[0],  Held[4],  Held[8],  Held[12] },
        { Held[1],  Held[5],  Held[9],  Held[13] },
        { Held[2],  Held[6],  Held[10], Held[14] },
        { Held[3],  Held[7],  Held[11], Held[15] }
    };

    double Derived[PlaneCount][4];

    for (std::uint32_t Index = 0u; Index < 4u; ++Index)
    {
        Derived[0][Index] = Rows[3][Index] + Rows[0][Index];   // leftward
        Derived[1][Index] = Rows[3][Index] - Rows[0][Index];   // rightward
        Derived[2][Index] = Rows[3][Index] + Rows[1][Index];   // lower
        Derived[3][Index] = Rows[3][Index] - Rows[1][Index];   // upper

        // 📝 🔴 With reversed depth the two depth planes exchange roles: the constraint that clip depth stays
        //    non-negative is the **furthest** plane, and the constraint that it does not exceed w is the nearest.
        //    Naming them by the row they came from would name them backwards for every reader.
        Derived[4][Index] = Rows[2][Index];                      // furthest
        Derived[5][Index] = Rows[3][Index] - Rows[2][Index];   // nearest
    }

    for (std::uint32_t PlaneIndex = 0u; PlaneIndex < PlaneCount; ++PlaneIndex)
    {
        const double NormalX = Derived[PlaneIndex][0];
        const double NormalY = Derived[PlaneIndex][1];
        const double NormalZ = Derived[PlaneIndex][2];

        const double Length = std::sqrt(NormalX * NormalX + NormalY * NormalY + NormalZ * NormalZ);

        if (Length <= 0.0)
        {
            Planes[PlaneIndex] = FrustumPlane{};
            continue;
        }

        const double Reciprocal = 1.0 / Length;

        Planes[PlaneIndex].NormalX  = NormalX * Reciprocal;
        Planes[PlaneIndex].NormalY  = NormalY * Reciprocal;
        Planes[PlaneIndex].NormalZ  = NormalZ * Reciprocal;

        // 🔴 Pushed outward, never inward. The margin is relative to the plane's own distance so it stays
        //    meaningful at every scene scale, with an absolute floor so a plane through the origin still moves.
        const double Distance = Derived[PlaneIndex][3] * Reciprocal;
        const double Margin   = FrustumOutwardMargin * (std::fabs(Distance) + 1.0);

        Planes[PlaneIndex].Constant = Distance + Margin;
    }

    PlanesDerived = true;
}

std::int32_t FrustumSpace::Classify(DocumentPosition Minimum, DocumentPosition Maximum) const
{
    if (!PlanesDerived)
        return 0;

    const DevicePosition RebasedMinimum    = Rebase(Minimum,    RebasingOrigin);
    const DevicePosition RebasedMaximum = Rebase(Maximum, RebasingOrigin);

    const double MinimumX    = static_cast<double>(RebasedMinimum.PositionX);
    const double MinimumY    = static_cast<double>(RebasedMinimum.PositionY);
    const double MinimumZ    = static_cast<double>(RebasedMinimum.PositionZ);
    const double MaximumX = static_cast<double>(RebasedMaximum.PositionX);
    const double MaximumY = static_cast<double>(RebasedMaximum.PositionY);
    const double MaximumZ = static_cast<double>(RebasedMaximum.PositionZ);

    std::int32_t Resolved = 1;

    for (std::uint32_t PlaneIndex = 0u; PlaneIndex < PlaneCount; ++PlaneIndex)
    {
        const FrustumPlane& Held = Planes[PlaneIndex];

        // 🔴 Keep the selected support vertices distinct from the box bounds. The previous locals
        //    shadowed `MaximumX`/`MinimumX` in their own initialisers and therefore read indeterminate
        //    values, making camera and marquee classification depend on the stack contents.
        const double PositiveX = Held.NormalX >= 0.0 ? MaximumX : MinimumX;
        const double PositiveY = Held.NormalY >= 0.0 ? MaximumY : MinimumY;
        const double PositiveZ = Held.NormalZ >= 0.0 ? MaximumZ : MinimumZ;

        const double NegativeX = Held.NormalX >= 0.0 ? MinimumX : MaximumX;
        const double NegativeY = Held.NormalY >= 0.0 ? MinimumY : MaximumY;
        const double NegativeZ = Held.NormalZ >= 0.0 ? MinimumZ : MaximumZ;

        const double Furthest = Held.NormalX * PositiveX
                              + Held.NormalY * PositiveY
                              + Held.NormalZ * PositiveZ + Held.Constant;

        if (Furthest < 0.0)
            return -1;

        const double Nearest = Held.NormalX * NegativeX
                             + Held.NormalY * NegativeY
                             + Held.NormalZ * NegativeZ + Held.Constant;

        if (Nearest < 0.0)
            Resolved = 0;
    }

    return Resolved;
}

bool FrustumSpace::Contains(DocumentPosition Subject) const
{
    return Classify(Subject, Subject) >= 0;
}

const FrustumPlane& FrustumSpace::Plane(std::uint32_t PlaneIndex) const
{
    return Planes[PlaneIndex < PlaneCount ? PlaneIndex : 0u];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     NAVIGATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> NavigationSequence::Open(NavigationSubject Declaring_, const CameraSpecification& Current)
{
    if (OpenDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "a navigation gesture is already open" });

    PriorCamera   = Current;
    AmendedCamera = Current;
    Declaring     = Declaring_;
    OpenDeclared  = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> NavigationSequence::Amend(double DisplacementX, double DisplacementY)
{
    if (!OpenDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no navigation gesture is open" });

    DocumentPosition& Position = AmendedCamera.Placement.Translation;
    RotationQuaternion Rotation = AmendedCamera.Placement.Rotation;

    const double FocusSpanX = Position.PositionX - AmendedCamera.FocusPosition.PositionX;
    const double FocusSpanY = Position.PositionY - AmendedCamera.FocusPosition.PositionY;
    const double FocusSpanZ = Position.PositionZ - AmendedCamera.FocusPosition.PositionZ;

    double FocusDistance = std::sqrt(FocusSpanX * FocusSpanX + FocusSpanY * FocusSpanY + FocusSpanZ * FocusSpanZ);

    if (FocusDistance <= 0.0)
        FocusDistance = AmendedCamera.Clipping.Nearest;

    switch (Declaring)
    {
        case NavigationSubject::Orbit:
        {
            // 📐 Yaw is taken about the document's own second axis and pitch about the camera's right axis **after**
            //    the yaw. Taking pitch about the axis before the yaw accumulates roll, and the horizon tilts a
            //    little more with every orbit until the artist cannot straighten it.
            const RotationQuaternion Yaw = RotationAbout(0.0, 1.0, 0.0,
                                                         -DisplacementX * OrbitRadiansPerPixel);

            const RotationQuaternion Yawed = Compound(Yaw, Rotation);

            double RightX = 0.0;
            double RightY = 0.0;
            double RightZ = 0.0;
            RotateSpan(Yawed, 1.0, 0.0, 0.0, RightX, RightY, RightZ);

            const RotationQuaternion Pitch = RotationAbout(RightX, RightY, RightZ,
                                                           -DisplacementY * OrbitRadiansPerPixel);

            AmendedCamera.Placement.Rotation = Compound(Pitch, Yawed);

            double YawedX = 0.0, YawedY = 0.0, YawedZ = 0.0;
            RotateSpan(Yaw, FocusSpanX, FocusSpanY, FocusSpanZ, YawedX, YawedY, YawedZ);

            double PitchedX = 0.0, PitchedY = 0.0, PitchedZ = 0.0;
            RotateSpan(Pitch, YawedX, YawedY, YawedZ, PitchedX, PitchedY, PitchedZ);

            Position.PositionX = AmendedCamera.FocusPosition.PositionX + PitchedX;
            Position.PositionY = AmendedCamera.FocusPosition.PositionY + PitchedY;
            Position.PositionZ = AmendedCamera.FocusPosition.PositionZ + PitchedZ;

            return Deliver<bool>::Result(true);
        }

        case NavigationSubject::Pan:
        {
            double RightX = 0.0, RightY = 0.0, RightZ = 0.0;
            double UpwardX = 0.0, UpwardY = 0.0, UpwardZ = 0.0;

            RotateSpan(Rotation, 1.0, 0.0, 0.0, RightX,  RightY,  RightZ);
            RotateSpan(Rotation, 0.0, 1.0, 0.0, UpwardX, UpwardY, UpwardZ);

            const double RightScale  = -DisplacementX  * PanFractionPerPixel * FocusDistance;
            const double UpwardScale =  DisplacementY * PanFractionPerPixel * FocusDistance;

            Position.PositionX += RightX * RightScale + UpwardX * UpwardScale;
            Position.PositionY += RightY * RightScale + UpwardY * UpwardScale;
            Position.PositionZ += RightZ * RightScale + UpwardZ * UpwardScale;

            // 📝 The focus travels with a pan, so a subsequent orbit turns about what the artist is now looking at
            //    rather than about the position they panned away from.
            AmendedCamera.FocusPosition.PositionX += RightX * RightScale + UpwardX * UpwardScale;
            AmendedCamera.FocusPosition.PositionY += RightY * RightScale + UpwardY * UpwardScale;
            AmendedCamera.FocusPosition.PositionZ += RightZ * RightScale + UpwardZ * UpwardScale;

            return Deliver<bool>::Result(true);
        }

        case NavigationSubject::Dolly:
        {
            double ForwardX = 0.0, ForwardY = 0.0, ForwardZ = 0.0;
            ViewDirection(Rotation, ForwardX, ForwardY, ForwardZ);

            const double Advance = -DisplacementY * DollyFractionPerPixel * FocusDistance;

            Position.PositionX += ForwardX * Advance;
            Position.PositionY += ForwardY * Advance;
            Position.PositionZ += ForwardZ * Advance;

            return Deliver<bool>::Result(true);
        }

        case NavigationSubject::Zoom:
        {
            // 📝 Multiplicative, so the same displacement produces the same proportional change at every field.
            //    An additive zoom crosses zero at the narrow end and inverts the projection.
            const double Scale = 1.0 + DisplacementY * ZoomFractionPerPixel;

            double Amending = AmendedCamera.ExtentParameter * (Scale > 0.0 ? Scale : 1.0);

            if (AmendedCamera.Projected == ProjectionSubject::Perspective)
                Amending = Amending < 1.0 ? 1.0 : (Amending > 179.0 ? 179.0 : Amending);
            else if (Amending <= 0.0)
                Amending = AmendedCamera.ExtentParameter;

            AmendedCamera.ExtentParameter = Amending;

            return Deliver<bool>::Result(true);
        }

        case NavigationSubject::NavigationCount:
            break;
    }

    return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the declared gesture has no amendment" });
}

Deliver<CameraSpecification> NavigationSequence::Abandon()
{
    if (!OpenDeclared)
        return Deliver<CameraSpecification>::Refuse({ RefusalReason::HostDenied, "no navigation gesture is open" });

    const CameraSpecification Restored = PriorCamera;

    AmendedCamera = PriorCamera;
    OpenDeclared  = false;

    return Deliver<CameraSpecification>::Result(Restored);
}

Deliver<CameraSpecification> NavigationSequence::Seal()
{
    if (!OpenDeclared)
        return Deliver<CameraSpecification>::Refuse({ RefusalReason::HostDenied, "no navigation gesture is open" });

    const CameraSpecification Sealed = AmendedCamera;

    OpenDeclared = false;

    return Deliver<CameraSpecification>::Result(Sealed);
}

const CameraSpecification& NavigationSequence::Amended() const { return AmendedCamera; }
bool                       NavigationSequence::GestureOpen() const { return OpenDeclared; }

//------------------------------------------------------------------------------------------------------------------------
//                                                      FRAMING
//------------------------------------------------------------------------------------------------------------------------

Deliver<DecomposedTransform> Frame(const CameraSpecification& Current,
                                   DocumentPosition           Minimum,
                                   DocumentPosition           Maximum)
{
    if (Maximum.PositionX < Minimum.PositionX
     || Maximum.PositionY < Minimum.PositionY
     || Maximum.PositionZ < Minimum.PositionZ)
    {
        return Deliver<DecomposedTransform>::Refuse(
            { RefusalReason::ContentUnsupported, "the extent is inverted and contains nothing" });
    }

    if (!Current.Clipping.IntervalValid() || Current.ExtentParameter <= 0.0 || Current.SensorProportion <= 0.0)
    {
        return Deliver<DecomposedTransform>::Refuse(
            { RefusalReason::ContentUnsupported, "the camera declares no projection to frame against" });
    }

    DocumentPosition Centre;
    Centre.PositionX = (Minimum.PositionX + Maximum.PositionX) * 0.5;
    Centre.PositionY = (Minimum.PositionY + Maximum.PositionY) * 0.5;
    Centre.PositionZ = (Minimum.PositionZ + Maximum.PositionZ) * 0.5;

    const double HalfX = (Maximum.PositionX - Minimum.PositionX) * 0.5;
    const double HalfY = (Maximum.PositionY - Minimum.PositionY) * 0.5;
    const double HalfZ = (Maximum.PositionZ - Minimum.PositionZ) * 0.5;

    double Radius = std::sqrt(HalfX * HalfX + HalfY * HalfY + HalfZ * HalfZ);

    if (Radius <= 0.0)
        Radius = Current.Clipping.Nearest;

    double Distance = Radius + Current.Clipping.Nearest;

    if (Current.Projected == ProjectionSubject::Perspective)
    {
        // 📐 The extent is contained on both axes, so the lesser of the two half-angles decides. Solving against
        //    the vertical alone frames correctly on a tall display and cuts the extent off on a wide one.
        const double HalfVerticalAngle = Current.ExtentParameter * 0.5 * Pi / 180.0;
        const double HalfHorizontalAngle = std::atan(std::tan(HalfVerticalAngle) * Current.SensorProportion);
        const double HalfLesser = HalfHorizontalAngle < HalfVerticalAngle ? HalfHorizontalAngle : HalfVerticalAngle;

        const double Sine = std::sin(HalfLesser);

        if (Sine > 0.0)
            Distance = Radius / Sine;
    }

    double ForwardX = 0.0;
    double ForwardY = 0.0;
    double ForwardZ = 0.0;
    ViewDirection(Current.Placement.Rotation, ForwardX, ForwardY, ForwardZ);

    DecomposedTransform Framed = Current.Placement;

    Framed.Translation.PositionX = Centre.PositionX - ForwardX * Distance;
    Framed.Translation.PositionY = Centre.PositionY - ForwardY * Distance;
    Framed.Translation.PositionZ = Centre.PositionZ - ForwardZ * Distance;

    return Deliver<DecomposedTransform>::Result(Framed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CAMERA
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> CameraProjection::Declare(OwnerIdentity Subject, const CameraSpecification& Declaring)
{
    if (!Subject.IdentityDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "a camera declares no owner" });

    CameraOwner = Subject;
    Specification  = Declaring;
    ReconcileOwed  = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> CameraProjection::Amend(const CameraSpecification& Amending)
{
    if (!CameraOwner.IdentityDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "no camera has been declared" });

    Specification = Amending;
    ReconcileOwed = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> CameraProjection::DeclareDisplayExtent(std::uint32_t Width, std::uint32_t Height)
{
    if (Width == 0u || Height == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a display extent of zero has no proportion" });

    Specification.SensorProportion = static_cast<double>(Width) / static_cast<double>(Height);
    ReconcileOwed                  = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> CameraProjection::Reconcile()
{
    const Deliver<ViewProjection> Derived = Derive(Specification);

    if (!Derived.Resolved)
        return Deliver<bool>::Refuse(Derived.Error);

    DerivedView = Derived.Resolve();
    DerivedFrustum.DeriveFrustumPlanes(DerivedView);

    ReconcileOwed = false;

    return Deliver<bool>::Result(true);
}

const CameraSpecification& CameraProjection::Declared() const   { return Specification;  }
const ViewProjection&      CameraProjection::Projected() const  { return DerivedView;    }
const FrustumSpace&        CameraProjection::Frustum() const    { return DerivedFrustum; }
OwnerIdentity           CameraProjection::Owner() const   { return CameraOwner; }
double                     CameraProjection::Exposure() const   { return Specification.Exposure; }
bool                       CameraProjection::DerivationOwed() const { return ReconcileOwed; }

}   // namespace Slate
