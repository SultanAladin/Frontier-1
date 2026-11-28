/*============================================================================================================================================
                                                          COORDINATECONVERSION.JS
============================================================================================================================================*/
// Orthonormal Right-Handed Coordinate Basis Conversion (det = +1, preserves CCW winding):
// RightHandedYUp (x, y, z) -> RightHandedZUp (x, -z, y)
// RightHandedZUp (X, Y, Z) -> RightHandedYUp (X, Z, -Y)

export const PiScalar      = 3.14159265358979323846;
export const TauScalar     = 6.28318530717958647692;
export const DegToRad      = PiScalar / 180.0;
export const RadToDeg      = 180.0 / PiScalar;

export function SrgbHexToLinearRgb(HexRgb)
{
    const DecodeChannel = (ByteVal) =>
    {
        const S = (ByteVal & 0xFF) / 255.0;
        return S <= 0.04045 ? S / 12.92 : Math.pow((S + 0.055) / 1.055, 2.4);
    };
    return [
        DecodeChannel((HexRgb >>> 16) & 0xFF),
        DecodeChannel((HexRgb >>> 8) & 0xFF),
        DecodeChannel(HexRgb & 0xFF),
    ];
}

export function BlackbodyKelvinToLinearTint(Kelvin)
{
    const Temp = Math.max(1000.0, Math.min(40000.0, Kelvin)) / 100.0;
    let R = 1.0, G = 1.0, B = 1.0;
    if (Temp <= 66.0)
    {
        R = 1.0;
        G = Math.max(0.0, Math.min(1.0, (99.4708025861 * Math.log(Temp) - 161.1195681661) / 255.0));
        B = Temp <= 19.0
            ? 0.0
            : Math.max(0.0, Math.min(1.0, (138.5177312231 * Math.log(Temp - 10.0) - 305.0447927307) / 255.0));
    }
    else
    {
        R = Math.max(0.0, Math.min(1.0, (329.698727446 * Math.pow(Temp - 60.0, -0.1332047592)) / 255.0));
        G = Math.max(0.0, Math.min(1.0, (288.1221695283 * Math.pow(Temp - 60.0, -0.0755148492)) / 255.0));
        B = 1.0;
    }
    const LinR = Math.pow(R, 2.2);
    const LinG = Math.pow(G, 2.2);
    const LinB = Math.pow(B, 2.2);
    const Lum  = Math.max(1e-4, 0.2126 * LinR + 0.7152 * LinG + 0.0722 * LinB);
    return [LinR / Lum, LinG / Lum, LinB / Lum];
}

export function EvaluateDot(A, B)
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

export function EvaluateCross(A, B)
{
    return [
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
    ];
}

export function EvaluateLengthSquared(V)
{
    return V[0] * V[0] + V[1] * V[1] + V[2] * V[2];
}

export function EvaluateLength(V)
{
    return Math.hypot(V[0], V[1], V[2]);
}

export function NormalizeAxis(V, Fallback = [0.0, 0.0, 1.0])
{
    const Len = Math.hypot(V[0], V[1], V[2]);
    if (Len <= 1.0e-8) return [Fallback[0], Fallback[1], Fallback[2]];
    const Inv = 1.0 / Len;
    return [V[0] * Inv, V[1] * Inv, V[2] * Inv];
}

export function SubtractAxis(A, B)
{
    return [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
}

export function AddAxis(A, B)
{
    return [A[0] + B[0], A[1] + B[1], A[2] + B[2]];
}

export function ScaleAxis(V, S)
{
    return [V[0] * S, V[1] * S, V[2] * S];
}

export function RotateYUpVector(Pt, PitchRad, YawRad, RollRad)
{
    let X = Pt[0], Y = Pt[1], Z = Pt[2];
    if (Math.abs(RollRad) > 1.0e-6)
    {
        const Cr = Math.cos(RollRad), Sr = Math.sin(RollRad);
        const Nx = X * Cr - Y * Sr;
        const Ny = X * Sr + Y * Cr;
        X = Nx; Y = Ny;
    }
    if (Math.abs(PitchRad) > 1.0e-6)
    {
        const Cp = Math.cos(PitchRad), Sp = Math.sin(PitchRad);
        const Ny = Y * Cp - Z * Sp;
        const Nz = Y * Sp + Z * Cp;
        Y = Ny; Z = Nz;
    }
    if (Math.abs(YawRad) > 1.0e-6)
    {
        const Cy = Math.cos(YawRad), Sy = Math.sin(YawRad);
        const Nx = X * Cy + Z * Sy;
        const Nz = -X * Sy + Z * Cy;
        X = Nx; Z = Nz;
    }
    return [X, Y, Z];
}

export function ConvertYUpPositionToWorldZUp(Hx, Hy, Hz)
{
    return [Hx, -Hz, Hy];
}

export function ConvertWorldZUpToYUp(Zup)
{
    return [Zup[0], Zup[2], -Zup[1]];
}

export function ConvertYUpToWorldZUp(Pt, Transform = null)
{
    const Scale = Transform?.UniformScale ?? 1.0;
    const Pitch = (Transform?.PitchDegrees ?? 0.0) * DegToRad;
    const Yaw   = (Transform?.YawDegrees ?? 0.0) * DegToRad;
    const Roll  = (Transform?.RollDegrees ?? 0.0) * DegToRad;
    const Scaled  = [Pt[0] * Scale, Pt[1] * Scale, Pt[2] * Scale];
    const Rotated = RotateYUpVector(Scaled, Pitch, Yaw, Roll);
    return [Rotated[0], -Rotated[2], Rotated[1]];
}

export function ConvertYUpNormalToWorldZUp(Normal, Transform = null)
{
    const Pitch = (Transform?.PitchDegrees ?? 0.0) * DegToRad;
    const Yaw   = (Transform?.YawDegrees ?? 0.0) * DegToRad;
    const Roll  = (Transform?.RollDegrees ?? 0.0) * DegToRad;
    const Rotated = RotateYUpVector(Normal, Pitch, Yaw, Roll);
    return NormalizeAxis([Rotated[0], -Rotated[2], Rotated[1]], [0.0, 0.0, 1.0]);
}

export function ComputeYUpAimAngles(FromYUp, TargetYUp)
{
    const Dx    = TargetYUp[0] - FromYUp[0];
    const Dy    = TargetYUp[1] - FromYUp[1];
    const Dz    = TargetYUp[2] - FromYUp[2];
    const Horiz = Math.max(1.0e-5, Math.hypot(Dx, Dz));
    const YawDeg   = Math.atan2(Dx, Dz) * RadToDeg;
    const PitchDeg = Math.atan2(-Dy, Horiz) * RadToDeg;
    return { YawDeg, PitchDeg };
}

export function ConstructAimedAreaQuadLight(CenterWorld, AimTargetWorld, WidthMeters, HeightMeters, Radiance, TwoSided = false, Title = "")
{
    const AimDir = NormalizeAxis(SubtractAxis(AimTargetWorld, CenterWorld), [0.0, 0.0, -1.0]);
    const RefUp  = Math.abs(AimDir[2]) < 0.96 ? [0.0, 0.0, 1.0] : [0.0, 1.0, 0.0];
    let Right    = NormalizeAxis(EvaluateCross(RefUp, AimDir), [1.0, 0.0, 0.0]);
    if (Math.abs(AimDir[2]) >= 0.96) Right = [1.0, 0.0, 0.0];
    const Up     = NormalizeAxis(EvaluateCross(AimDir, Right), [0.0, -1.0, 0.0]);
    return {
        Title,
        CenterWorld,
        RightTangent:     Right,
        UpBitangent:      Up,
        NormalWorld:      AimDir,
        HalfWidthMeters:  WidthMeters * 0.5,
        HalfHeightMeters: HeightMeters * 0.5,
        EmittedRadiance:  [Radiance[0], Radiance[1], Radiance[2]],
        TwoSidedEmission: Boolean(TwoSided),
    };
}

export function ConstructAimedPhotometricSpotLight(PosWorld, TargetWorld, PeakRgb, RangeMeters, ProfileOrdinal = 1, Title = "")
{
    const BeamAxis = NormalizeAxis(SubtractAxis(TargetWorld, PosWorld), [0.0, 0.0, -1.0]);
    const RefUp    = Math.abs(BeamAxis[2]) < 0.96 ? [0.0, 0.0, 1.0] : [1.0, 0.0, 0.0];
    const ZeroAz   = NormalizeAxis(EvaluateCross(RefUp, BeamAxis), [1.0, 0.0, 0.0]);
    const OrthoUp  = NormalizeAxis(EvaluateCross(BeamAxis, ZeroAz), [0.0, 1.0, 0.0]);
    return {
        Title,
        PositionWorld:    PosWorld,
        BeamAxis,
        ZeroAzimuthAxis:  ZeroAz,
        OrthoUpAxis:      OrthoUp,
        PeakIntensityRgb: [PeakRgb[0], PeakRgb[1], PeakRgb[2]],
        RangeMeters,
        RadiusMeters:     0.12,
        ProfileOrdinal,
    };
}

export function EvaluateOrthonormalCameraBasis(Eye, Target)
{
    const Forward = NormalizeAxis(SubtractAxis(Target, Eye), [0.0, 1.0, 0.0]);
    const WorldUp = Math.abs(Forward[2]) < 0.985 ? [0.0, 0.0, 1.0] : [0.0, 1.0, 0.0];
    const Right   = NormalizeAxis(EvaluateCross(Forward, WorldUp), [1.0, 0.0, 0.0]);
    const Up      = NormalizeAxis(EvaluateCross(Right, Forward), WorldUp);
    return { Forward, Right, Up };
}

export function ConstructPerspectiveViewClip(Eye, Right, Up, Forward, VerticalFovDeg, AspectRatio, NearPlane, FarPlane)
{
    const TanHalfFov = Math.tan(Math.max(5.0, Math.min(160.0, VerticalFovDeg)) * DegToRad * 0.5);
    const SafeAspect = Math.max(1e-4, AspectRatio);
    const ScaleX     = 1.0 / Math.max(1e-5, SafeAspect * TanHalfFov);
    const ScaleY     = 1.0 / Math.max(1e-5, TanHalfFov); // WebGPU NDC Y is +Up in [-1, 1]
    const SafeNear   = Math.max(0.02, NearPlane);
    const SafeFar    = Math.max(SafeNear + 1.0, FarPlane);
    const ZRange     = SafeFar / (SafeNear - SafeFar);

    const View = [
        [Right[0],    Right[1],    Right[2],    -EvaluateDot(Right, Eye)],
        [Up[0],       Up[1],       Up[2],       -EvaluateDot(Up, Eye)],
        [-Forward[0], -Forward[1], -Forward[2],  EvaluateDot(Forward, Eye)],
        [0.0,         0.0,         0.0,          1.0],
    ];
    const Proj = [
        [ScaleX, 0.0,    0.0,    0.0],
        [0.0,    ScaleY, 0.0,    0.0],
        [0.0,    0.0,    ZRange, SafeNear * ZRange],
        [0.0,    0.0,   -1.0,    0.0],
    ];

    // Column-major Float32Array(16) for WGSL mat4x4f
    const ColMajor = new Float32Array(16);
    for (let Row = 0; Row < 4; ++Row)
    {
        for (let Col = 0; Col < 4; ++Col)
        {
            const Val =
                Proj[Row][0] * View[0][Col] +
                Proj[Row][1] * View[1][Col] +
                Proj[Row][2] * View[2][Col] +
                Proj[Row][3] * View[3][Col];
            ColMajor[Col * 4 + Row] = Val;
        }
    }
    return { Matrix: ColMajor, TanHalfFov, Aspect: SafeAspect };
}
