/*====================================================================================================================================
                                                       LINEARALGEBRA.JS
====================================================================================================================================*/
// 🧩 Column-major 4x4 transform algebra and 3-component vector arithmetic for the combat viewport

//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

export function Vec3Add(Left, Right)
{
    return [Left[0] + Right[0], Left[1] + Right[1], Left[2] + Right[2]];
}

export function Vec3Subtract(Left, Right)
{
    return [Left[0] - Right[0], Left[1] - Right[1], Left[2] - Right[2]];
}

export function Vec3Scale(Source, Multiplier)
{
    return [Source[0] * Multiplier, Source[1] * Multiplier, Source[2] * Multiplier];
}

export function Vec3Dot(Left, Right)
{
    return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2];
}

export function Vec3Cross(Left, Right)
{
    return [
        Left[1] * Right[2] - Left[2] * Right[1],
        Left[2] * Right[0] - Left[0] * Right[2],
        Left[0] * Right[1] - Left[1] * Right[0]
    ];
}

export function Vec3Length(Source)
{
    return Math.sqrt(Source[0] * Source[0] + Source[1] * Source[1] + Source[2] * Source[2]);
}

export function Vec3Normalize(Source)
{
    const Magnitude = Vec3Length(Source);                           // [-] - Euclidean norm; zero-guarded below
    if (Magnitude < 1e-8) return [0, 0, 0];
    return [Source[0] / Magnitude, Source[1] / Magnitude, Source[2] / Magnitude];
}

// Planar separation in the ground plane only — vertical offset ignored, which is what chassis proximity tests want.
export function Vec3GroundDistance(Left, Right)
{
    const SeparationX = Left[0] - Right[0];                         // [m] - Lateral separation
    const SeparationZ = Left[2] - Right[2];                         // [m] - Depth separation
    return Math.sqrt(SeparationX * SeparationX + SeparationZ * SeparationZ);
}

// 📝 Shortest signed sweep from one heading to another across the ±π seam; keeps turret tracking from taking the long way.
export function ShortestAngularSweep(FromAngle, ToAngle)
{
    let AngularSweep = (ToAngle - FromAngle) % (Math.PI * 2.0);     // [rad] - Raw difference, wrapped once
    if (AngularSweep >  Math.PI) AngularSweep -= Math.PI * 2.0;
    if (AngularSweep < -Math.PI) AngularSweep += Math.PI * 2.0;
    return AngularSweep;
}

export function ClampScalar(Source, MinimumBoundary, MaximumBoundary)
{
    if (Source < MinimumBoundary) return MinimumBoundary;
    if (Source > MaximumBoundary) return MaximumBoundary;
    return Source;
}

export function InterpolateScalar(FromScalar, ToScalar, Interpolant)
{
    return FromScalar + (ToScalar - FromScalar) * Interpolant;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   MATRIX CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

export function Mat4Identity()
{
    return new Float32Array([1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1]);
}

// Column-major product Left * Right, matching the WGSL matrix convention so buffers upload without transposition.
export function Mat4Multiply(Left, Right)
{
    const Product = new Float32Array(16);
    for (let ColumnIndex = 0; ColumnIndex < 4; ++ColumnIndex)
    {
        for (let RowIndex = 0; RowIndex < 4; ++RowIndex)
        {
            Product[ColumnIndex * 4 + RowIndex] =
                Left[0 * 4 + RowIndex] * Right[ColumnIndex * 4 + 0] +
                Left[1 * 4 + RowIndex] * Right[ColumnIndex * 4 + 1] +
                Left[2 * 4 + RowIndex] * Right[ColumnIndex * 4 + 2] +
                Left[3 * 4 + RowIndex] * Right[ColumnIndex * 4 + 3];
        }
    }
    return Product;
}

// 📝 Right-handed projection emitting a 0..1 depth range — the convention WebGPU's depth attachment expects.
export function Mat4Perspective(VerticalFieldOfView, AspectRatio, NearPlane, FarPlane)
{
    const FocalScale  = 1.0 / Math.tan(VerticalFieldOfView * 0.5);  // [-] - Cotangent of the half angle
    const DepthExtent = NearPlane - FarPlane;                       // [m] - Negative depth span

    const Projection = new Float32Array(16);
    Projection[0]  = FocalScale / AspectRatio;
    Projection[5]  = FocalScale;
    Projection[10] = FarPlane / DepthExtent;
    Projection[11] = -1.0;
    Projection[14] = NearPlane * FarPlane / DepthExtent;
    return Projection;
}

// ⚠️ Standard right-handed basis — do NOT "fix" perceived control inversion by flipping LateralAxis here. Doing so
//    drives VerticalAxis to point downward and renders the whole field upside down. Yaw sense belongs to the chassis
//    forward convention (see ChassisForwardAxis in CombatSimulation), not to the camera basis.
export function Mat4LookDirection(EyePosition, TargetPosition, UpDirection)
{
    const BackwardAxis = Vec3Normalize(Vec3Subtract(EyePosition, TargetPosition));
    const LateralAxis  = Vec3Normalize(Vec3Cross(UpDirection, BackwardAxis));
    const VerticalAxis = Vec3Cross(BackwardAxis, LateralAxis);

    return new Float32Array([
        LateralAxis[0],  VerticalAxis[0],  BackwardAxis[0],  0,
        LateralAxis[1],  VerticalAxis[1],  BackwardAxis[1],  0,
        LateralAxis[2],  VerticalAxis[2],  BackwardAxis[2],  0,
        -Vec3Dot(LateralAxis,  EyePosition),
        -Vec3Dot(VerticalAxis, EyePosition),
        -Vec3Dot(BackwardAxis, EyePosition),
        1
    ]);
}

// Assemble a model transform as translation ∘ yaw ∘ elevation ∘ non-uniform scale, evaluated analytically to avoid
// four products. Geometry is authored +Z-forward, so yaw follows the (sin, cos) forward convention the simulation uses.
//
// 🔴 ElevationAngle is POSITIVE-IS-NOSE-UP, which is deliberately NOT the textbook rotation-about-+X (that pitches a
//    +Z-forward object's nose DOWN for a positive angle). Every consumer in this prototype — the bore axis, the sight
//    target, the turret pitch limits, the reticle elevation bias, the ballistic solver — already means "positive is
//    up", so the negation lives here, once, instead of at five call sites. Feeding a textbook +X rotation in here
//    instead makes the barrel visually elevate DOWNWARD while the camera and the shell both go up.
export function Mat4Assemble(Translation, YawAngle, ElevationAngle, Scale)
{
    const YawCosine       = Math.cos(YawAngle);
    const YawSine         = Math.sin(YawAngle);
    const ElevationCosine = Math.cos(ElevationAngle);
    const ElevationSine   = Math.sin(ElevationAngle);

    // 📝 Columns are Yaw(Y) * Elevation(X) already scaled per axis; column 2 is where local +Z lands, and its Y
    //    component is +sin(elevation) so a raised angle raises the nose.
    return new Float32Array([
         YawCosine * Scale[0],                            0.0 * Scale[0],           -YawSine * Scale[0],           0,
        -YawSine * ElevationSine * Scale[1],   ElevationCosine * Scale[1], -YawCosine * ElevationSine * Scale[1],  0,
         YawSine * ElevationCosine * Scale[2],  ElevationSine * Scale[2],   YawCosine * ElevationCosine * Scale[2], 0,
         Translation[0], Translation[1], Translation[2], 1
    ]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATRIX INVERSION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Full 4x4 inverse by cofactor expansion. Needed because chassis parts carry non-uniform scale, so the shader
//    cannot reuse the model matrix for normals — it needs this inverse transposed.
export function Mat4Invert(Source)
{
    const A00 = Source[0],  A01 = Source[1],  A02 = Source[2],  A03 = Source[3];
    const A10 = Source[4],  A11 = Source[5],  A12 = Source[6],  A13 = Source[7];
    const A20 = Source[8],  A21 = Source[9],  A22 = Source[10], A23 = Source[11];
    const A30 = Source[12], A31 = Source[13], A32 = Source[14], A33 = Source[15];

    const Minor00 = A00 * A11 - A01 * A10;
    const Minor01 = A00 * A12 - A02 * A10;
    const Minor02 = A00 * A13 - A03 * A10;
    const Minor03 = A01 * A12 - A02 * A11;
    const Minor04 = A01 * A13 - A03 * A11;
    const Minor05 = A02 * A13 - A03 * A12;
    const Minor06 = A20 * A31 - A21 * A30;
    const Minor07 = A20 * A32 - A22 * A30;
    const Minor08 = A20 * A33 - A23 * A30;
    const Minor09 = A21 * A32 - A22 * A31;
    const Minor10 = A21 * A33 - A23 * A31;
    const Minor11 = A22 * A33 - A23 * A32;

    let Determinant = Minor00 * Minor11 - Minor01 * Minor10 + Minor02 * Minor09 +
                      Minor03 * Minor08 - Minor04 * Minor07 + Minor05 * Minor06;
    if (Math.abs(Determinant) < 1e-12) return Mat4Identity();
    Determinant = 1.0 / Determinant;

    return new Float32Array([
        ( A11 * Minor11 - A12 * Minor10 + A13 * Minor09) * Determinant,
        (-A01 * Minor11 + A02 * Minor10 - A03 * Minor09) * Determinant,
        ( A31 * Minor05 - A32 * Minor04 + A33 * Minor03) * Determinant,
        (-A21 * Minor05 + A22 * Minor04 - A23 * Minor03) * Determinant,
        (-A10 * Minor11 + A12 * Minor08 - A13 * Minor07) * Determinant,
        ( A00 * Minor11 - A02 * Minor08 + A03 * Minor07) * Determinant,
        (-A30 * Minor05 + A32 * Minor02 - A33 * Minor01) * Determinant,
        ( A20 * Minor05 - A22 * Minor02 + A23 * Minor01) * Determinant,
        ( A10 * Minor10 - A11 * Minor08 + A13 * Minor06) * Determinant,
        (-A00 * Minor10 + A01 * Minor08 - A03 * Minor06) * Determinant,
        ( A30 * Minor04 - A31 * Minor02 + A33 * Minor00) * Determinant,
        (-A20 * Minor04 + A21 * Minor02 - A23 * Minor00) * Determinant,
        (-A10 * Minor09 + A11 * Minor07 - A12 * Minor06) * Determinant,
        ( A00 * Minor09 - A01 * Minor07 + A02 * Minor06) * Determinant,
        (-A30 * Minor03 + A31 * Minor01 - A32 * Minor00) * Determinant,
        ( A20 * Minor03 - A21 * Minor01 + A22 * Minor00) * Determinant
    ]);
}

export function Mat4Transpose(Source)
{
    return new Float32Array([
        Source[0], Source[4], Source[8],  Source[12],
        Source[1], Source[5], Source[9],  Source[13],
        Source[2], Source[6], Source[10], Source[14],
        Source[3], Source[7], Source[11], Source[15]
    ]);
}

// The normal transform: inverse transpose of the model matrix, with translation stripped so directions stay directions.
export function Mat4NormalTransform(ModelTransform)
{
    const Inverted = Mat4Transpose(Mat4Invert(ModelTransform));
    Inverted[3] = 0.0; Inverted[7] = 0.0; Inverted[11] = 0.0;
    Inverted[12] = 0.0; Inverted[13] = 0.0; Inverted[14] = 0.0; Inverted[15] = 1.0;
    return Inverted;
}
