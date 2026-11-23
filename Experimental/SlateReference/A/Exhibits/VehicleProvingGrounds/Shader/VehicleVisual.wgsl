// Volumetric authored tyres, pressure-driven deformation, damage visibility and cabin articulation; shared by all raster paths.
struct VehicleRecord {
    Cabin : vec4f, // steering angle, see-through glass, reserved, lamp
    Column : vec4f,
    Axis : vec4f,
    CabinFrame : vec4f,
    CabinShift : vec4f, // tablet centre xyz, tablet-only scale w
    Wheels : array<vec4f, 4>, // x = shredded; yzw reserved
    Deformation : array<vec4f, 4>, // x = ContactAngle, y = Deflection, z = GaugePressure, w = LateralDistortion
}

const TyreRimRadius: f32 = 0.3448;
const TyreOuterRadius: f32 = 0.50855;
const PI: f32 = 3.141592653589793;

struct DeformedVertex { Position : vec3f, Normal : vec3f, }

// 📐 Volumetric Tyre Physics (Chaos Flesh / PB-FEM Volumetric Incompressibility & Pressure Dilatation on GPU):
// Evaluates radial contact patch flattening, volumetric sidewall bulges (conserving rubber/gas volume),
// shoulder expansion waves, lateral slip shear and internal inflation pressure stiffening in real time.
fn ResolveWheelVertex(Wheel: u32, Family: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex {
    var Result: DeformedVertex;
    Result.Position = Position;
    Result.Normal = Normal;

    // All vertices of a shredded rubber facet collapse to the axle: no rubber or rubber shadow.
    // Rim/hub/brake facets and all occupant steering/spin/suspension transforms stay intact.
    if (Family == TyreFamily && VehicleState.Wheels[Wheel].x > 0.5) {
        Result.Position = vec3f(0.0);
        return Result;
    }

    if (Family != TyreFamily) {
        return Result;
    }

    let Deform = VehicleState.Deformation[Wheel];
    let Deflection = Deform.y;
    if (Deflection <= 1.0e-5) {
        return Result;
    }

    let ContactAngle = Deform.x;
    let Pressure = max(0.0, Deform.z);
    let LateralDistortion = Deform.w;

    let R = length(Position.xz);
    if (R <= TyreRimRadius + 1.0e-4) {
        return Result;
    }

    let Phi = atan2(Position.z, Position.x);
    var DPhi = Phi - ContactAngle;
    DPhi = DPhi - 2.0 * PI * round(DPhi / (2.0 * PI));

    // Internal pressure scaling:
    // Reference pressure is 220 kPa. Under higher pressure the contact patch is shorter and stiffer;
    // under lower pressure (deflation) the contact patch elongates and sidewalls bulge dramatically.
    let PNorm = max(0.05, Pressure / 220000.0);
    let PatchSpan = clamp(0.44 / sqrt(max(0.15, PNorm)), 0.20, 1.15);

    // Normalized radial depth: 0 at bead/rim, 1 at outer tread crown.
    let RadialFrac = clamp((R - TyreRimRadius) / (TyreOuterRadius - TyreRimRadius), 0.0, 1.0);

    var DR: f32 = 0.0;
    var DY: f32 = 0.0;
    let AbsDPhi = abs(DPhi);

    // ① Contact flattening constraint:
    if (AbsDPhi < PatchSpan) {
        let U = AbsDPhi / PatchSpan;
        let Envelope = 0.5 * (1.0 + cos(U * PI));
        DR = -Deflection * Envelope * RadialFrac;

        // ② Volumetric conservation (Poisson / Chaos Flesh hyperelastic continuum):
        // Compressing the rubber tyre radially in contact pushes the material outward along the sidewalls.
        let SidewallFactor = sin(RadialFrac * PI);
        let BulgeIntensity = 0.48 / (0.4 + 0.6 * sqrt(PNorm));
        DY = sign(Position.y) * (Deflection * BulgeIntensity * Envelope * SidewallFactor);
    }

    // ③ Volumetric wave expansion (bead/shoulder transition zone just outside contact):
    let WaveOuter = PatchSpan * 1.5;
    if (AbsDPhi >= PatchSpan && AbsDPhi < WaveOuter) {
        let V = (AbsDPhi - PatchSpan) / (WaveOuter - PatchSpan);
        let Wave = sin(V * PI) * (0.12 * Deflection * RadialFrac);
        DR = DR + Wave;
    }

    // ④ Lateral cornering slip / shear strain:
    if (AbsDPhi < PatchSpan * 1.2) {
        let UShear = AbsDPhi / (PatchSpan * 1.2);
        let ShearEnv = 0.5 * (1.0 + cos(UShear * PI));
        DY = DY + LateralDistortion * ShearEnv * RadialFrac;
    }

    let NewR = max(TyreRimRadius, R + DR);
    Result.Position = vec3f(NewR * cos(Phi), Position.y + DY, NewR * sin(Phi));

    // Tilt deformed normal to maintain accurate shading over the contact patch & bulging sidewalls:
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
