//============================================================================================================================================
//                                                          TRANSFORMPROJECTION.JS
//============================================================================================================================================
// 🧩 Vector, quaternion and column-major 4×4 transforms between the authored, world, view and clip spaces (+Z up, reversed depth).

//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

export function AddVector(Left, Right)
{
    return [Left[0] + Right[0], Left[1] + Right[1], Left[2] + Right[2]];
}

export function SubtractVector(Left, Right)
{
    return [Left[0] - Right[0], Left[1] - Right[1], Left[2] - Right[2]];
}

export function ScaleVector(Vector, Factor)
{
    return [Vector[0] * Factor, Vector[1] * Factor, Vector[2] * Factor];
}

export function MultiplyAddVector(Origin, Direction, Factor)
{
    return [Origin[0] + Direction[0] * Factor, Origin[1] + Direction[1] * Factor, Origin[2] + Direction[2] * Factor];
}

export function DotVector(Left, Right)
{
    return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2];
}

export function CrossVector(Left, Right)
{
    return [Left[1] * Right[2] - Left[2] * Right[1],
            Left[2] * Right[0] - Left[0] * Right[2],
            Left[0] * Right[1] - Left[1] * Right[0]];
}

export function LengthVector(Vector)
{
    return Math.hypot(Vector[0], Vector[1], Vector[2]);
}

export function NormalizeVector(Vector)
{
    const Length = LengthVector(Vector);
    return Length > 1.0e-12 ? ScaleVector(Vector, 1.0 / Length) : [0.0, 0.0, 1.0];
}

export function InterpolateVector(Left, Right, Fraction)
{
    return [Left[0] + (Right[0] - Left[0]) * Fraction,
            Left[1] + (Right[1] - Left[1]) * Fraction,
            Left[2] + (Right[2] - Left[2]) * Fraction];
}

export function ClampScalar(Scalar, Minimum, Maximum)
{
    return Scalar < Minimum ? Minimum : (Scalar > Maximum ? Maximum : Scalar);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  QUATERNION ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

// 📝 Quaternions are [x, y, z, w]; Hamilton product, active rotation q·v·q*.
export function MultiplyQuaternion(Left, Right)
{
    return [Left[3] * Right[0] + Left[0] * Right[3] + Left[1] * Right[2] - Left[2] * Right[1],
            Left[3] * Right[1] - Left[0] * Right[2] + Left[1] * Right[3] + Left[2] * Right[0],
            Left[3] * Right[2] + Left[0] * Right[1] - Left[1] * Right[0] + Left[2] * Right[3],
            Left[3] * Right[3] - Left[0] * Right[0] - Left[1] * Right[1] - Left[2] * Right[2]];
}

export function NormalizeQuaternion(Quaternion)
{
    const Length = Math.hypot(Quaternion[0], Quaternion[1], Quaternion[2], Quaternion[3]);
    return Length > 1.0e-12 ? Quaternion.map((Component) => Component / Length) : [0.0, 0.0, 0.0, 1.0];
}

export function ConjugateQuaternion(Quaternion)
{
    return [-Quaternion[0], -Quaternion[1], -Quaternion[2], Quaternion[3]];
}

export function ConstructAxisQuaternion(Axis, θ)
{
    const Half = 0.5 * θ;
    const Sine = Math.sin(Half);
    return [Axis[0] * Sine, Axis[1] * Sine, Axis[2] * Sine, Math.cos(Half)];
}

// 📐 v' = v + 2w(q×v) + 2q×(q×v), the expanded sandwich product without forming q* explicitly.
export function RotateVector(Quaternion, Vector)
{
    const Axis = [Quaternion[0], Quaternion[1], Quaternion[2]];
    const Twice = ScaleVector(CrossVector(Axis, Vector), 2.0);
    return AddVector(AddVector(Vector, ScaleVector(Twice, Quaternion[3])), CrossVector(Axis, Twice));
}

// 📝 Integrates an angular velocity ω [rad/s] over Δτ [s] into an orientation: q ← q + ½·(ω,0)·q·Δτ, renormalised.
export function IntegrateQuaternion(Quaternion, ω, Δτ)
{
    const Spin = MultiplyQuaternion([ω[0], ω[1], ω[2], 0.0], Quaternion);
    return NormalizeQuaternion([Quaternion[0] + 0.5 * Spin[0] * Δτ,
                                Quaternion[1] + 0.5 * Spin[1] * Δτ,
                                Quaternion[2] + 0.5 * Spin[2] * Δτ,
                                Quaternion[3] + 0.5 * Spin[3] * Δτ]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               COLUMN-MAJOR TRANSFORMS
//------------------------------------------------------------------------------------------------------------------------

export function ConstructIdentityTransform()
{
    const Transform = new Float32Array(16);
    Transform[0] = Transform[5] = Transform[10] = Transform[15] = 1.0;
    return Transform;
}

// 📝 Result = Left × Right, both column-major, so Result applied to a point is Left(Right(point)).
export function MultiplyTransform(Left, Right)
{
    const Result = new Float32Array(16);
    for (let Column = 0; Column < 4; ++Column)
    {
        for (let Row = 0; Row < 4; ++Row)
        {
            let Sum = 0.0;
            for (let Inner = 0; Inner < 4; ++Inner)
                Sum += Left[Inner * 4 + Row] * Right[Column * 4 + Inner];
            Result[Column * 4 + Row] = Sum;
        }
    }
    return Result;
}

// 📐 General 4×4 inverse by cofactor expansion; returns identity for a singular input rather than NaN.
export function InvertTransform(Source)
{
    const Inverse = new Float32Array(16);
    const S = Source;
    Inverse[0]  =  S[5] * S[10] * S[15] - S[5] * S[11] * S[14] - S[9] * S[6] * S[15] + S[9] * S[7] * S[14] + S[13] * S[6] * S[11] - S[13] * S[7] * S[10];
    Inverse[4]  = -S[4] * S[10] * S[15] + S[4] * S[11] * S[14] + S[8] * S[6] * S[15] - S[8] * S[7] * S[14] - S[12] * S[6] * S[11] + S[12] * S[7] * S[10];
    Inverse[8]  =  S[4] * S[9]  * S[15] - S[4] * S[11] * S[13] - S[8] * S[5] * S[15] + S[8] * S[7] * S[13] + S[12] * S[5] * S[11] - S[12] * S[7] * S[9];
    Inverse[12] = -S[4] * S[9]  * S[14] + S[4] * S[10] * S[13] + S[8] * S[5] * S[14] - S[8] * S[6] * S[13] - S[12] * S[5] * S[10] + S[12] * S[6] * S[9];
    Inverse[1]  = -S[1] * S[10] * S[15] + S[1] * S[11] * S[14] + S[9] * S[2] * S[15] - S[9] * S[3] * S[14] - S[13] * S[2] * S[11] + S[13] * S[3] * S[10];
    Inverse[5]  =  S[0] * S[10] * S[15] - S[0] * S[11] * S[14] - S[8] * S[2] * S[15] + S[8] * S[3] * S[14] + S[12] * S[2] * S[11] - S[12] * S[3] * S[10];
    Inverse[9]  = -S[0] * S[9]  * S[15] + S[0] * S[11] * S[13] + S[8] * S[1] * S[15] - S[8] * S[3] * S[13] - S[12] * S[1] * S[11] + S[12] * S[3] * S[9];
    Inverse[13] =  S[0] * S[9]  * S[14] - S[0] * S[10] * S[13] - S[8] * S[1] * S[14] + S[8] * S[2] * S[13] + S[12] * S[1] * S[10] - S[12] * S[2] * S[9];
    Inverse[2]  =  S[1] * S[6]  * S[15] - S[1] * S[7]  * S[14] - S[5] * S[2] * S[15] + S[5] * S[3] * S[14] + S[13] * S[2] * S[7]  - S[13] * S[3] * S[6];
    Inverse[6]  = -S[0] * S[6]  * S[15] + S[0] * S[7]  * S[14] + S[4] * S[2] * S[15] - S[4] * S[3] * S[14] - S[12] * S[2] * S[7]  + S[12] * S[3] * S[6];
    Inverse[10] =  S[0] * S[5]  * S[15] - S[0] * S[7]  * S[13] - S[4] * S[1] * S[15] + S[4] * S[3] * S[13] + S[12] * S[1] * S[7]  - S[12] * S[3] * S[5];
    Inverse[14] = -S[0] * S[5]  * S[14] + S[0] * S[6]  * S[13] + S[4] * S[1] * S[14] - S[4] * S[2] * S[13] - S[12] * S[1] * S[6]  + S[12] * S[2] * S[5];
    Inverse[3]  = -S[1] * S[6]  * S[11] + S[1] * S[7]  * S[10] + S[5] * S[2] * S[11] - S[5] * S[3] * S[10] - S[9]  * S[2] * S[7]  + S[9]  * S[3] * S[6];
    Inverse[7]  =  S[0] * S[6]  * S[11] - S[0] * S[7]  * S[10] - S[4] * S[2] * S[11] + S[4] * S[3] * S[10] + S[8]  * S[2] * S[7]  - S[8]  * S[3] * S[6];
    Inverse[11] = -S[0] * S[5]  * S[11] + S[0] * S[7]  * S[9]  + S[4] * S[1] * S[11] - S[4] * S[3] * S[9]  - S[8]  * S[1] * S[7]  + S[8]  * S[3] * S[5];
    Inverse[15] =  S[0] * S[5]  * S[10] - S[0] * S[6]  * S[9]  - S[4] * S[1] * S[10] + S[4] * S[2] * S[9]  + S[8]  * S[1] * S[6]  - S[8]  * S[2] * S[5];
    const Determinant = S[0] * Inverse[0] + S[1] * Inverse[4] + S[2] * Inverse[8] + S[3] * Inverse[12];
    if (Math.abs(Determinant) < 1.0e-20)
        return ConstructIdentityTransform();
    const Reciprocal = 1.0 / Determinant;
    for (let Index = 0; Index < 16; ++Index)
        Inverse[Index] *= Reciprocal;
    return Inverse;
}

// 📝 T · R · S for a position, an orientation quaternion and a per-axis scale (a negative axis mirrors).
export function ConstructRigidTransform(Position, Quaternion, Scale = [1.0, 1.0, 1.0])
{
    const [x, y, z, w] = Quaternion;
    const Transform = new Float32Array(16);
    Transform[0]  = (1.0 - 2.0 * (y * y + z * z)) * Scale[0];
    Transform[1]  = (2.0 * (x * y + z * w))       * Scale[0];
    Transform[2]  = (2.0 * (x * z - y * w))       * Scale[0];
    Transform[4]  = (2.0 * (x * y - z * w))       * Scale[1];
    Transform[5]  = (1.0 - 2.0 * (x * x + z * z)) * Scale[1];
    Transform[6]  = (2.0 * (y * z + x * w))       * Scale[1];
    Transform[8]  = (2.0 * (x * z + y * w))       * Scale[2];
    Transform[9]  = (2.0 * (y * z - x * w))       * Scale[2];
    Transform[10] = (1.0 - 2.0 * (x * x + y * y)) * Scale[2];
    Transform[12] = Position[0];
    Transform[13] = Position[1];
    Transform[14] = Position[2];
    Transform[15] = 1.0;
    return Transform;
}

export function TransformPoint(Transform, Point)
{
    const x = Point[0], y = Point[1], z = Point[2];
    const w = Transform[3] * x + Transform[7] * y + Transform[11] * z + Transform[15];
    const Reciprocal = Math.abs(w) > 1.0e-20 ? 1.0 / w : 1.0;
    return [(Transform[0] * x + Transform[4] * y + Transform[8]  * z + Transform[12]) * Reciprocal,
            (Transform[1] * x + Transform[5] * y + Transform[9]  * z + Transform[13]) * Reciprocal,
            (Transform[2] * x + Transform[6] * y + Transform[10] * z + Transform[14]) * Reciprocal];
}

export function TransformDirection(Transform, Direction)
{
    const x = Direction[0], y = Direction[1], z = Direction[2];
    return [Transform[0] * x + Transform[4] * y + Transform[8]  * z,
            Transform[1] * x + Transform[5] * y + Transform[9]  * z,
            Transform[2] * x + Transform[6] * y + Transform[10] * z];
}

// 📝 World-space axis-aligned bounds of a local box carried through an affine transform (Arvo's method).
export function TransformBounds(Transform, LocalMinimum, LocalMaximum)
{
    const Centre = ScaleVector(AddVector(LocalMinimum, LocalMaximum), 0.5);
    const Half   = ScaleVector(SubtractVector(LocalMaximum, LocalMinimum), 0.5);
    const WorldCentre = TransformPoint(Transform, Centre);
    const WorldHalf = [0.0, 0.0, 0.0];
    for (let Row = 0; Row < 3; ++Row)
    {
        WorldHalf[Row] = Math.abs(Transform[Row]) * Half[0] + Math.abs(Transform[4 + Row]) * Half[1]
                       + Math.abs(Transform[8 + Row]) * Half[2];
    }
    return { Minimum: SubtractVector(WorldCentre, WorldHalf), Maximum: AddVector(WorldCentre, WorldHalf) };
}

//------------------------------------------------------------------------------------------------------------------------
//                                              VIEW AND CLIP PROJECTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Right-handed view: the camera looks down its −Z, +Y up on screen; the world is +Z up.
export function ConstructViewTransform(Eye, Target, Up = [0.0, 0.0, 1.0])
{
    const Forward = NormalizeVector(SubtractVector(Target, Eye));
    let Right = CrossVector(Forward, Up);
    if (LengthVector(Right) < 1.0e-6)
        Right = CrossVector(Forward, [0.0, 1.0, 0.0]);
    Right = NormalizeVector(Right);
    const Upward = CrossVector(Right, Forward);
    const View = new Float32Array(16);
    View[0] = Right[0]; View[4] = Right[1]; View[8]  = Right[2];
    View[1] = Upward[0]; View[5] = Upward[1]; View[9]  = Upward[2];
    View[2] = -Forward[0]; View[6] = -Forward[1]; View[10] = -Forward[2];
    View[12] = -DotVector(Right, Eye);
    View[13] = -DotVector(Upward, Eye);
    View[14] = DotVector(Forward, Eye);
    View[15] = 1.0;
    return View;
}

// 📐 Reversed, infinite-far perspective: z_ndc = near / −z_view, so depth is 1 at the near plane and → 0 at infinity.
//    Jitter is an NDC offset folded into the third column (clip.xy += jitter · clip.w).
export function ConstructPerspectiveTransform(VerticalFieldOfView, Aspect, Near, JitterX = 0.0, JitterY = 0.0)
{
    const Focal = 1.0 / Math.tan(0.5 * VerticalFieldOfView);
    const Projection = new Float32Array(16);
    Projection[0]  = Focal / Aspect;
    Projection[5]  = Focal;
    Projection[8]  = -JitterX;
    Projection[9]  = -JitterY;
    Projection[11] = -1.0;
    Projection[14] = Near;
    return Projection;
}

// 📝 Forward-depth orthographic projection mapping z_view ∈ [−Far, −Near] onto [0, 1] (shadow depth, depth test less).
export function ConstructOrthographicTransform(Left, Right, Bottom, Top, Near, Far)
{
    const Projection = new Float32Array(16);
    Projection[0]  = 2.0 / (Right - Left);
    Projection[5]  = 2.0 / (Top - Bottom);
    Projection[10] = -1.0 / (Far - Near);
    Projection[12] = -(Right + Left) / (Right - Left);
    Projection[13] = -(Top + Bottom) / (Top - Bottom);
    Projection[14] = -Near / (Far - Near);
    Projection[15] = 1.0;
    return Projection;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                LOW-DISCREPANCY JITTER
//------------------------------------------------------------------------------------------------------------------------

// 📐 Radical inverse in base b — the Halton sequence the sub-pixel jitter walks (bases 2 and 3, period 16).
export function ProjectRadicalInverse(Index, Radix)
{
    let Fraction = 1.0;
    let Result = 0.0;
    let Remaining = Index;
    while (Remaining > 0)
    {
        Fraction /= Radix;
        Result += Fraction * (Remaining % Radix);
        Remaining = Math.floor(Remaining / Radix);
    }
    return Result;
}
