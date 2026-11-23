/*====================================================================================================================================
                                                    ORBITPROJECTION.JS
====================================================================================================================================*/
// 🧩 Orbit camera and the view/projection matrices the surface and dab passes share

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Pitch is clamped short of the poles. At exactly ±90° the forward axis becomes parallel to the world
//    up axis and the right-vector cross product collapses to zero, which detonates the basis into NaN.
const PitchLimit       = 1.5533430342749532;    // [rad] - 89 degrees
const DistanceMinimum  = 0.05;                  // [-]   - Orbit radius floor
const DistanceMaximum  = 200.0;                 // [-]   - Orbit radius ceiling

// 📝 Near/far are tied to the orbit radius so precision follows the zoom. The ratio between them is what
//    costs depth bits: at near=0.002*d and far=12*d the range is 6000:1 and every sample sits at
//    ndc.z ≈ 0.998, leaving nothing to resolve the surface against itself. 0.05..4.0 is an 80:1 range,
//    which still clears Suzanne front-to-back with room to spare and keeps depth spread across [0,1].
const NearPlaneRatio   = 0.05;                  // [-]   - Near plane as a fraction of the orbit radius
const FarPlaneRatio    = 4.0;                   // [-]   - Far plane as a multiple of the orbit radius

const OrbitRadianPerPixel = 0.006;              // [rad/px]
const PanUnitPerPixel     = 0.0022;             // [-/px]
const ZoomPerNotch        = 0.0011;             // [-]

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

function Normalize(Vector)
{
    const Magnitude = Math.hypot(Vector[0], Vector[1], Vector[2]);
    if (Magnitude < 1e-9) return [0.0, 0.0, 0.0];
    return [Vector[0] / Magnitude, Vector[1] / Magnitude, Vector[2] / Magnitude];
}

function Cross(Left, Right)
{
    return [
        Left[1] * Right[2] - Left[2] * Right[1],
        Left[2] * Right[0] - Left[0] * Right[2],
        Left[0] * Right[1] - Left[1] * Right[0]
    ];
}

function Dot(Left, Right)
{
    return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2];
}

// 📝 Column-major, matching WGSL's mat4x4f constructor and std140 column layout. Writing these
//    row-major and letting the shader read them transposed is the classic silent-garbage bug here.
//
// 🔴 This view matrix is LEFT-handed on purpose: the third row is +Forward, so view-space z grows with
//    distance in front of the camera. It MUST stay paired with the projection below, whose c2.w is +1
//    and which therefore reads w = +z_view. Negating Forward here (the GL convention) makes every w
//    negative, putting the whole surface behind the near plane — the geometry vanishes with no error,
//    no shader diagnostic, and perfectly plausible-looking x/y values. Measured before the fix:
//    all 7 sample vertices at w around -4.5.
function ComposeView(Eye, Target, WorldUp)
{
    // 📝 Basis order matters as much as the sign of Forward. With Forward kept positive, Right must be
    //    Cross(Forward, WorldUp) and Up must be Cross(Right, Forward); swapping either cross mirrors the
    //    image left-to-right, which on a near-symmetric head is almost impossible to spot by eye.
    const Forward = Normalize([Target[0] - Eye[0], Target[1] - Eye[1], Target[2] - Eye[2]]);
    const Right   = Normalize(Cross(Forward, WorldUp));
    const Up      = Cross(Right, Forward);

    return new Float32Array([
        Right[0],         Up[0],         Forward[0],        0.0,
        Right[1],         Up[1],         Forward[1],        0.0,
        Right[2],         Up[2],         Forward[2],        0.0,
        -Dot(Right, Eye), -Dot(Up, Eye), -Dot(Forward, Eye), 1.0
    ]);
}

// 📝 WebGPU clip space is z in [0, 1], NOT OpenGL's [-1, 1]. A GL-style projection here would push
//    half the surface behind the near plane and the depth test would cull it silently.
function ComposePerspective(VerticalFieldRadian, AspectRatio, NearPlane, FarPlane)
{
    const FocalScale = 1.0 / Math.tan(VerticalFieldRadian * 0.5);
    const DepthRange = FarPlane - NearPlane;

    return new Float32Array([
        FocalScale / AspectRatio, 0.0,        0.0,                                  0.0,
        0.0,                      FocalScale, 0.0,                                  0.0,
        0.0,                      0.0,        FarPlane / DepthRange,                1.0,
        0.0,                      0.0,        -(FarPlane * NearPlane) / DepthRange, 0.0
    ]);
}

function MultiplyMatrix(Left, Right)
{
    const Product = new Float32Array(16);

    for (let ColumnOrdinal = 0; ColumnOrdinal < 4; ColumnOrdinal += 1)
    {
        for (let RowOrdinal = 0; RowOrdinal < 4; RowOrdinal += 1)
        {
            let Accumulated = 0.0;
            for (let StepOrdinal = 0; StepOrdinal < 4; StepOrdinal += 1)
            {
                Accumulated += Left[StepOrdinal * 4 + RowOrdinal] * Right[ColumnOrdinal * 4 + StepOrdinal];
            }
            Product[ColumnOrdinal * 4 + RowOrdinal] = Accumulated;
        }
    }

    return Product;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

export class OrbitProjection
{
    constructor(Framing)
    {
        this.Target   = [...(Framing?.Centre ?? [0.0, 0.0, 0.0])];
        this.Distance = (Framing?.Radius ?? 1.0) * 2.4;

        // 📝 Suzanne faces -Z in the OBJ, so a yaw of 0 with the eye pushed along +Z looks at her face.
        this.Yaw   = 0.0;                       // [rad]
        this.Pitch = 0.18;                      // [rad] - slightly above the eye line
        this.VerticalField = 0.7853981633974483;// [rad] - 45 degrees

        this.WorldUp = [0.0, 1.0, 0.0];         // Suzanne is Y-up
    }

    EyePosition()
    {
        const HorizontalRadius = this.Distance * Math.cos(this.Pitch);
        return [
            this.Target[0] + HorizontalRadius * Math.sin(this.Yaw),
            this.Target[1] + this.Distance    * Math.sin(this.Pitch),
            this.Target[2] + HorizontalRadius * Math.cos(this.Yaw)
        ];
    }

    Orbit(HorizontalPixels, VerticalPixels)
    {
        this.Yaw   -= HorizontalPixels * OrbitRadianPerPixel;
        this.Pitch += VerticalPixels   * OrbitRadianPerPixel;
        this.Pitch  = Math.max(-PitchLimit, Math.min(PitchLimit, this.Pitch));
    }

    // 📝 Pan moves the target across the view plane, and the step scales with distance so the surface
    //    tracks the cursor at any zoom instead of crawling when close and bolting when far.
    Pan(HorizontalPixels, VerticalPixels)
    {
        const Eye     = this.EyePosition();
        const Forward = Normalize([this.Target[0] - Eye[0], this.Target[1] - Eye[1], this.Target[2] - Eye[2]]);
        const Right   = Normalize(Cross(Forward, this.WorldUp));
        const Up      = Cross(Right, Forward);

        const Step = PanUnitPerPixel * this.Distance;

        for (let AxisOrdinal = 0; AxisOrdinal < 3; AxisOrdinal += 1)
        {
            this.Target[AxisOrdinal] += (-Right[AxisOrdinal] * HorizontalPixels + Up[AxisOrdinal] * VerticalPixels) * Step;
        }
    }

    // 📝 Multiplicative zoom, so each notch changes the radius by a constant proportion. An additive
    //    step would stall far out and punch through the surface up close.
    Zoom(NotchDelta)
    {
        this.Distance *= Math.exp(NotchDelta * ZoomPerNotch);
        this.Distance  = Math.max(DistanceMinimum, Math.min(DistanceMaximum, this.Distance));
    }

    // Assemble every matrix and vector the passes consume for the current orbit state.
    Resolve(SurfaceWidth, SurfaceHeight)
    {
        const Eye         = this.EyePosition();
        const AspectRatio = SurfaceWidth / Math.max(1, SurfaceHeight);

        const NearPlane = Math.max(1e-4, this.Distance * NearPlaneRatio);
        const FarPlane  = this.Distance * FarPlaneRatio;

        const View       = ComposeView(Eye, this.Target, this.WorldUp);
        const Projection = ComposePerspective(this.VerticalField, AspectRatio, NearPlane, FarPlane);

        return {
            Eye,
            View,
            Projection,
            ViewProjection: MultiplyMatrix(Projection, View),
            NearPlane,
            FarPlane
        };
    }
}
