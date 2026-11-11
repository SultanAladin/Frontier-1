//============================================================================================================================================
//                                                         VEHICLESPECIFICATION.JS
//============================================================================================================================================
// 🧩 The ControlVehicle as authored data — sockets, mass, wheels, struts, tyre and powertrain sheets from Frontier/GRIT.

//------------------------------------------------------------------------------------------------------------------------
//                                                CONTROLVEHICLE SOCKETS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Lifted verbatim from VehicleGeometry.h ControlVehicleSockets (read out of ControlVehicle.blend), model-origin frame.
export const ControlVehicleSockets = Object.freeze({
    AxleMount:       [[+1.7274, +1.0475, +0.0914], [+1.7274, -1.0475, +0.0914],
                      [-1.6686, +1.0475, +0.0914], [-1.6686, -1.0475, +0.0914]],
    SuspensionMount: [[+1.7274, +1.0475, +0.6436], [+1.7274, -1.0475, +0.6436],
                      [-1.6686, +1.0475, +0.6436], [-1.6686, -1.0475, +0.6436]],
    RearWingPort:    [-2.9822, 0.0000, +0.8367],
    IdPlatePrimary:  [+2.9842, 0.0000, +0.1006],
    IdPlateSecondary:[-2.9906, 0.0000, +0.2628]
});

export const WheelSlot = Object.freeze({ FrontLeft: 0, FrontRight: 1, RearLeft: 2, RearRight: 3 });

//------------------------------------------------------------------------------------------------------------------------
//                                                   VEHICLE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 VehicleGeometry.h defaults: mass 1300 kg, CoM 0.53 m above ground, measured tyre and rim radii.
export const VehicleGeometry = Object.freeze({
    Mass:                 1300.0,    // [kg]  incl. driver and fuel
    CoMHeight:            0.53,      // [m]   centre of mass above flat ground at rest
    TyreRadius:           0.50855,   // [m]   RubberFL Z/X span 1.0171 / 2
    TyreRimRadius:        0.34460,   // [m]   RimFL span 0.6892 / 2
    TyreWidth:            0.39580,   // [m]   RubberFL Y span
    AuthoredRimRadius:    0.344818,  // [m]   ControlWheelStructure kRimRadius
    UnsprungMassPerWheel: 20.0,      // [kg]  carcass, hub, upright, brake, half strut
    FrontWeightFraction:  0.4914,    // [-]   static front mass fraction
    RollRadiusGyration:   0.60,      // [m]   about body X
    PitchRadiusGyration:  1.55,      // [m]   about body Y
    YawRadiusGyration:    1.60       // [m]   about body Z
});

// 📐 The model origin is 0.41715 m above the ground: axle socket +0.0914 less the 0.50855 tyre radius.
export function ResolveAuthoredGroundOffset()
{
    return VehicleGeometry.TyreRadius - ControlVehicleSockets.AxleMount[0][2];
}

// 📐 Centre of mass expressed in the authored model frame (0.53 − 0.41715 = 0.11285 m), the body rebase.
export function ResolveCoMAuthoredZ()
{
    return VehicleGeometry.CoMHeight - ResolveAuthoredGroundOffset();
}

export function ResolveAxleMountLocal(Wheel)
{
    const Socket = ControlVehicleSockets.AxleMount[Wheel];
    return [Socket[0], Socket[1], Socket[2] - ResolveCoMAuthoredZ()];
}

export function ResolveSuspensionMountLocal(Wheel)
{
    const Socket = ControlVehicleSockets.SuspensionMount[Wheel];
    return [Socket[0], Socket[1], Socket[2] - ResolveCoMAuthoredZ()];
}

export function ResolveSprungMass()
{
    return Math.max(1.0, VehicleGeometry.Mass - 4.0 * VehicleGeometry.UnsprungMassPerWheel);
}

export function ResolveInertia()
{
    const Sprung = ResolveSprungMass();
    return [Sprung * VehicleGeometry.RollRadiusGyration ** 2,
            Sprung * VehicleGeometry.PitchRadiusGyration ** 2,
            Sprung * VehicleGeometry.YawRadiusGyration ** 2];
}

//------------------------------------------------------------------------------------------------------------------------
//                                               SUSPENSION SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 GRIT's strut, carried over whole by SuspensionSpecification.h: 2.0 Hz, ζ 0.6, digressive spring with a 1.5
//    hardening factor over 2 cm bump + 8 cm droop, rate and damping derived from the corner mass, never authored.
export class SuspensionSpecification
{
    constructor()
    {
        this.NaturalFrequency  = 2.0;       // [Hz]
        this.DampingRatio      = 0.6;       // [-]
        this.SpringRate        = 0.0;       // [N/m]    derived
        this.DampingRate       = 0.0;       // [N·s/m]  derived
        this.MaxTravel         = 0.35;      // [m]
        this.BumpTravel        = 0.02;      // [m]
        this.DroopTravel       = 0.08;      // [m]
        this.Preload           = 0.0;       // [N]      derived
        this.StaticPreload     = 0.05;      // [m]
        this.StaticCompression = 0.0;       // [m]      solved
        this.FreeLength        = 0.0;       // [m]      solved from sockets
        this.HardeningFactor   = 1.5;       // [-]
        this.Coefficients      = [0.0, 0.0, 0.0];   // k₁ [N/m], k₂ [N/m²], k₃ [N/m³]
    }

    // 📐 ω = 2πf, k = mω², c = 2ζmω; preload is GRIT's 2 cm carried over as a compression, not as its 5000 N.
    Calibrate(SprungMass)
    {
        const ω = 2.0 * Math.PI * this.NaturalFrequency;
        this.SpringRate  = SprungMass * ω * ω;
        this.DampingRate = 2.0 * this.DampingRatio * SprungMass * ω;
        this.Preload     = this.SpringRate * 0.02;
        const Range = this.BumpTravel + this.DroopTravel;
        this.Coefficients = [this.SpringRate * this.HardeningFactor,
                             -(this.SpringRate * (this.HardeningFactor - 1.0)) / Range,
                             0.0];
    }

    // 📐 F(x) = ∫₀ˣ max(k(u), 0.1·SpringRate) du — the floored digressive cubic that cannot invert past its turn.
    SpringForce(Compression)
    {
        const [k1, k2, k3] = this.Coefficients;
        const Cubic = (x) => k1 * x + k2 * x * x + k3 * x * x * x;
        const Floor = this.SpringRate * 0.1;
        const Turn  = this.RateFloorCompression();
        if (Turn <= 0.0 || Compression <= Turn)
            return Cubic(Compression);
        return Cubic(Turn) + Floor * (Compression - Turn);
    }

    RateFloorCompression()
    {
        const [k1, k2, k3] = this.Coefficients;
        const a = 3.0 * k3, b = 2.0 * k2, c = k1 - this.SpringRate * 0.1;
        if (Math.abs(a) < 1.0e-6)
        {
            if (Math.abs(b) < 1.0e-6)
                return 0.0;
            const Root = -c / b;
            return Root > 0.0 ? Root : 0.0;
        }
        const Discriminant = b * b - 4.0 * a * c;
        if (Discriminant < 0.0)
            return 0.0;
        const Root = Math.sqrt(Discriminant);
        const R1 = (-b - Root) / (2.0 * a), R2 = (-b + Root) / (2.0 * a);
        const Low = Math.min(R1, R2), High = Math.max(R1, R2);
        return Low > 0.0 ? Low : (High > 0.0 ? High : 0.0);
    }

    // 📐 Bisects Preload + F(x) = Load; the closed form g/ω² is only the linear, zero-preload case.
    StaticCompressionFor(SupportedLoad)
    {
        if (SupportedLoad <= this.Preload)
            return 0.0;
        let Low = 0.0, High = this.MaxTravel;
        for (let Iteration = 0; Iteration < 48; ++Iteration)
        {
            const Middle = 0.5 * (Low + High);
            if (this.Preload + this.SpringForce(Middle) < SupportedLoad)
                Low = Middle;
            else
                High = Middle;
        }
        return 0.5 * (Low + High);
    }

    // 📝 The model is posed at rest, so the socket separation is the STATIC length; the free length adds the sag back.
    ResolveFromSockets(MountHeight, AxleHeight, SupportedLoad)
    {
        this.StaticCompression = this.StaticCompressionFor(SupportedLoad);
        this.FreeLength = (MountHeight - AxleHeight) + this.StaticCompression;
    }
}

// 📝 Mirrors VehicleGeometry.cpp ResolveSuspension(): rate each axle for the sprung mass it carries.
export function ResolveSuspensionPair()
{
    const Sprung      = ResolveSprungMass();
    const Front       = new SuspensionSpecification();
    const Rear        = new SuspensionSpecification();
    const FrontCorner = 0.5 * Sprung * VehicleGeometry.FrontWeightFraction;
    const RearCorner  = 0.5 * Sprung * (1.0 - VehicleGeometry.FrontWeightFraction);
    Front.Calibrate(FrontCorner);
    Rear.Calibrate(RearCorner);
    const MountZ = ControlVehicleSockets.SuspensionMount[0][2];
    const AxleZ  = ControlVehicleSockets.AxleMount[0][2];
    Front.ResolveFromSockets(MountZ, AxleZ, FrontCorner * 9.81);
    Rear.ResolveFromSockets(MountZ, AxleZ, RearCorner * 9.81);
    return { Front, Rear };
}

export const AntiRollBars = Object.freeze({
    Front: { Stiffness: 80000.0, Damping: 1500.0, LeftWheel: 0, RightWheel: 1 },   // [N/m], [N·s/m]
    Rear:  { Stiffness: 80000.0, Damping: 1500.0, LeftWheel: 2, RightWheel: 3 }
});

//------------------------------------------------------------------------------------------------------------------------
//                                              TYRE AND POWERTRAIN SHEETS
//------------------------------------------------------------------------------------------------------------------------

// 📝 PacejkaMagicFormula.h PacejkaParameters defaults (GRIT FTireSpecSheet), MF6.1.
export const PacejkaParameters = Object.freeze({
    Fz0: 5.0, Lx: 1.2, Ly: 1.3, Ltr: 1.0,
    MuLoadSensitivityLong: 0.08, MuLoadSensitivityLat: 0.12, MuLoadMinFactor: 0.60,
    pBx1: 18.0, pBx2: 20.0, pBx3: 0.50, pCx1: 1.55, pDx1: 1.6, pDx2: -0.03, pDx3: 0.015,
    pEx1: -0.10, pEx2: -0.15, pEx3: 0.005, pEx4: 0.030, pHx1: 0.0, pHx2: 0.0, pVx1: 0.0, pVx2: 0.0,
    pBy1: 18.0, pBy2: 10.0, pBy3: 0.10, pCy1: 1.45, pDy1: 1.85, pDy2: -0.06, pDy3: 0.10,
    pEy1: -1.2, pEy2: -0.50, pEy3: 0.20, pHy1: 0.0, pHy2: 0.0, pVy1: 0.0, pVy2: 0.0, pVy3: 0.0, pVy4: 0.0,
    pCamber1: 0.12, pCamber2: -0.05,
    rBx1: 12.0, rBx2: 8.0, rBx3: 0.0, rCx1: 1.0, rEx1: 0.0, rEx2: 0.0, rHx1: 0.0,
    rBy1: 5.0, rBy2: 4.0, rBy3: 0.0, rBy4: 0.0, rCy1: 0.75, rEy1: 0.0, rEy2: 0.0, rHy1: 0.0, rHy2: 0.0,
    rVy1: 0.0, rVy2: 0.0, rVy3: 0.0, rVy4: 0.0, rVy5: 1.9, rVy6: 0.0,
    RelaxationLengthLong: 0.052, RelaxationLengthLat: 0.048,
    RelaxationLoadExponentLong: 0.4, RelaxationLoadExponentLat: 0.45
});

// 📝 Drivetrain.cpp EngineParameters::DefaultGTR() — 18-point wide-open-throttle curve, peak 520 N·m.
export const EngineSheet = Object.freeze({
    TorqueCurve: [[800, 234], [1200, 300], [1600, 352], [2000, 392], [2400, 428], [2800, 460], [3200, 486],
                  [3600, 504], [4000, 514], [4400, 518], [4800, 520], [5200, 520], [5800, 520], [6200, 506],
                  [6600, 486], [7000, 462], [7600, 448], [8500, 442]],
    IdleRPM:                700.0,
    RedlineRPM:             7200.0,
    EngineBrakingCoeff:     0.08,      // [N·m·s/rad]
    EngineBrakingQuadratic: 0.00012,   // [N·m·s²/rad²]
    EngineInertia:          0.38       // [kg·m²]
});

// 📝 TurbochargerParameters::DefaultGTR() boost-to-torque multiplier, used through a first-order spool lag.
export const TurbochargerSheet = Object.freeze({
    TorqueMultiplierCurve: [[0.0, 1.0], [0.5, 1.3], [1.0, 1.7], [1.2, 1.85]],
    WastegateMaxBoost:     1.2,    // [Bar]
    SpoolTimeConstant:     0.55    // [s]  first-order stand-in for the shaft integration
});

// 📝 TransmissionParameters defaults: [0]=R2, [1]=R1, [2]=N, [3..8]=1st..6th; GT-R final drive.
export const TransmissionSheet = Object.freeze({
    GearRatios:           [-3.8, -2.0, 0.0, 4.2, 3.1, 2.5, 1.9, 1.5, 1.2],
    FinalDriveRatio:      3.55,
    DriveEfficiency:      0.92,
    NeutralIndex:         2,
    ReverseIndex:         1,
    FirstIndex:           3,
    UpshiftRPM:           6800.0,
    DownshiftRPM:         2600.0,
    ShiftCooldownSeconds: 0.35,
    ClutchCapacity:       1200.0,   // [N·m]
    LaunchRPM:            2600.0    // [rpm] AMT slip target while the wheels cannot yet carry the engine
});

// 📝 VehicleSolverConfiguration defaults: brake and handbrake torque, steering lock and rate, wheel inertia.
export const ChassisSheet = Object.freeze({
    MaxSteerAngleRad:          0.52,     // [rad]
    SteerRatePerSecond:        6.0,      // [1/s]
    MaxBrakeTorquePerWheel:    2600.0,   // [N·m]
    HandbrakeTorque:           4200.0,   // [N·m]
    RollingResistance:         0.015,    // [-]
    WheelInertia:              1.2,      // [kg·m²]
    SpinDampingCoefficient:    1400.0,   // [N per m/s]
    LateralDampingCoefficient: 1600.0,   // [N per m/s]
    ContactDampingFadeSpeed:   6.0,      // [m/s]
    DragArea:                  0.74,     // [m²]  Cd·A of the GT3 package
    LiftArea:                  1.10,     // [m²]  Cl·A downforce, split by FrontLiftFraction
    FrontLiftFraction:         0.45,     // [-]
    AirDensity:                1.225     // [kg/m³]
});
