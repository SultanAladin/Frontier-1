// 3D FEM / Shell / XPBD carcass nodal displacement field, volumetric tyre deformation, damage visibility and cabin articulation; shared by all raster paths.
struct VehicleRecord {
    Cabin                : vec4f,             // steering angle, see-through glass, reserved, lamp
    Column               : vec4f,
    Axis                 : vec4f,
    CabinFrame           : vec4f,
    CabinShift           : vec4f,             // tablet centre xyz, tablet-only scale w
    Wheels               : array<vec4f, 4>,   // x = shredded, y = carcassActive (1 = 3D FEM / Shell / XPBD), zw = reserved
    Deformation          : array<vec4f, 4>,   // x = ContactAngle, y = Deflection, z = GaugePressure, w = LateralDistortion
    CarcassDisplacements : array<vec4f, 640>, // 4 wheels * 32 circumferential sectors * 5 meridional rings: wheel-local u = x - X
}

const TyreRimRadius   : f32 = 0.3448;
const TyreOuterRadius : f32 = 0.50855;
const TyreHalfWidth   : f32 = 0.118;
const PI              : f32 = 3.141592653589793;

struct DeformedVertex { Position : vec3f, Normal : vec3f, }

fn SampleCarcassDisplacement(Wheel: u32, Sector: u32, Ring: u32) -> vec3f {
    let Idx = Wheel * 160u + (Sector & 31u) * 5u + min(Ring, 4u);
    return VehicleState.CarcassDisplacements[Idx].xyz;
}

// 📐 Evaluates real-time GPU vertex deformation from either the 3D Hexahedral Neo-Hookean FEM / Shell / XPBD
//    32x5 nodal displacement lattice (Wheels[Wheel].y > 0.5) or the analytical volumetric spring profile.
fn ResolveWheelVertex(Wheel: u32, Family: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex {
    var Result: DeformedVertex;
    Result.Position = Position;
    Result.Normal = Normal;

    if (Family == TyreFamily && VehicleState.Wheels[Wheel].x > 0.5) {
        Result.Position = vec3f(0.0);
        return Result;
    }

    if (Family != TyreFamily) {
        return Result;
    }

    let R = length(Position.xz);
    if (R <= TyreRimRadius + 1.0e-4) {
        return Result;
    }

    let Deform     = VehicleState.Deformation[Wheel];
    let Deflection = Deform.y;
    if (Deflection <= 1.0e-5) {
        return Result;
    }

    let RadialFrac = clamp((R - TyreRimRadius) / (TyreOuterRadius - TyreRimRadius), 0.0, 1.0);
    let Phi        = atan2(Position.z, Position.x);

    // Path A: 3D FEM / 2D Shell FEM / XPBD Carcass Nodal Displacement Field Interpolation
    if (VehicleState.Wheels[Wheel].y > 0.5) {
        let NormAngle = fract(Phi / (2.0 * PI) + 1.0) * 32.0;
        let S0        = u32(floor(NormAngle)) & 31u;
        let S1        = (S0 + 1u) & 31u;
        let FracS     = fract(NormAngle);

        // Map lateral Y [-0.118..+0.118] across the 5 meridional rings (0 = inner bead, 1 = inner shoulder, 2 = crown, 3 = outer shoulder, 4 = outer bead)
        let RingCoord = clamp((Position.y / TyreHalfWidth) * 1.35 + 2.0, 0.5, 3.5);
        let M0        = min(u32(floor(RingCoord)), 3u);
        let M1        = M0 + 1u;
        let FracM     = clamp(RingCoord - f32(M0), 0.0, 1.0);

        let U00 = SampleCarcassDisplacement(Wheel, S0, M0);
        let U10 = SampleCarcassDisplacement(Wheel, S1, M0);
        let U01 = SampleCarcassDisplacement(Wheel, S0, M1);
        let U11 = SampleCarcassDisplacement(Wheel, S1, M1);

        let U0   = mix(U00, U10, FracS);
        let U1   = mix(U01, U11, FracS);
        let UFem = mix(U0, U1, FracM);

        let RadialWeight = smoothstep(0.0, 0.88, RadialFrac);
        Result.Position  = Position + UFem * RadialWeight;

        // Tangential finite-difference normal perturbation across the deformed sector
        let DUSector = (U10 - U00) * RadialWeight;
        let Tangent  = vec3f(-sin(Phi), 0.0, cos(Phi));
        let Tilt     = dot(DUSector, vec3f(cos(Phi), 0.0, sin(Phi))) * 4.5;
        Result.Normal = normalize(Normal - Tangent * Tilt);
        return Result;
    }

    // Path B: Analytical Spring Volumetric Profile Fallback
    let ContactAngle = Deform.x;
    let Pressure = max(0.0, Deform.z);
    let LateralDistortion = Deform.w;

    var DPhi = Phi - ContactAngle;
    DPhi = DPhi - 2.0 * PI * round(DPhi / (2.0 * PI));

    let PNorm = max(0.05, Pressure / 220000.0);
    let PatchSpan = clamp(0.44 / sqrt(max(0.15, PNorm)), 0.20, 1.15);

    var DR: f32 = 0.0;
    var DY: f32 = 0.0;
    let AbsDPhi = abs(DPhi);

    if (AbsDPhi < PatchSpan) {
        let U = AbsDPhi / PatchSpan;
        let Envelope = 0.5 * (1.0 + cos(U * PI));
        DR = -Deflection * Envelope * RadialFrac;

        let SidewallFactor = sin(RadialFrac * PI);
        let BulgeIntensity = 0.48 / (0.4 + 0.6 * sqrt(PNorm));
        DY = sign(Position.y) * (Deflection * BulgeIntensity * Envelope * SidewallFactor);
    }

    let WaveOuter = PatchSpan * 1.5;
    if (AbsDPhi >= PatchSpan && AbsDPhi < WaveOuter) {
        let V = (AbsDPhi - PatchSpan) / (WaveOuter - PatchSpan);
        let Wave = sin(V * PI) * (0.12 * Deflection * RadialFrac);
        DR = DR + Wave;
    }

    if (AbsDPhi < PatchSpan * 1.2) {
        let UShear = AbsDPhi / (PatchSpan * 1.2);
        let ShearEnv = 0.5 * (1.0 + cos(UShear * PI));
        DY = DY + LateralDistortion * ShearEnv * RadialFrac;
    }

    let NewR = max(TyreRimRadius, R + DR);
    Result.Position = vec3f(NewR * cos(Phi), Position.y + DY, NewR * sin(Phi));

    if (AbsDPhi < PatchSpan) {
        let Tilt = -sin((DPhi / PatchSpan) * PI) * (Deflection / (TyreOuterRadius * PatchSpan));
        Result.Normal = normalize(Normal + vec3f(-sin(Phi), 0.0, cos(Phi)) * Tilt);
    }

    return Result;
}

// 📐 Rodrigues: Vector turned by Angle about the unit Axis.
fn TurnAboutAxis(Vector: vec3f, Axis: vec3f, Angle: f32) -> vec3f
{
    let C = cos(Angle);
    let S = sin(Angle);
    return Vector * C + cross(Axis, Vector) * S + Axis * dot(Axis, Vector) * (1.0 - C);
}

// 📝 A steering-wheel facet (car-body frame) turned about the column by the wheel's angle.
fn TurnSteeringWheel(Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    var Turned: DeformedVertex;
    let Axis = normalize(VehicleState.Axis.xyz);
    let Pivot = VehicleState.Column.xyz;
    Turned.Position = Pivot + TurnAboutAxis(Position - Pivot, Axis, VehicleState.Cabin.x);
    Turned.Normal = TurnAboutAxis(Normal, Axis, VehicleState.Cabin.x);
    return Turned;
}

fn PlaceTablet(Position: vec3f) -> vec3f {
    return VehicleState.CabinShift.xyz + (Position - VehicleState.CabinShift.xyz) * VehicleState.CabinShift.w;
}
// Only the steering assembly articulates. HUD sizing never moves physical cabin geometry.
fn DeformBodyVertex(Family: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    var Moved: DeformedVertex;
    Moved.Position = Position;
    Moved.Normal = Normal;
    if (Family == TabletHousingFamily || Family == DisplayGlassFamily) { Moved.Position = PlaceTablet(Position); }
    if (Family == SteeringWheelFamily)
    {
        Moved = TurnSteeringWheel(Position, Normal);
    }
    return Moved;
}
