//============================================================================================================================================
//                                                             VEHICLESOLVER.JS
//============================================================================================================================================
// 🧩 ControlVehicle rigid-body dynamics: socket struts, transient-slip MF6.1 tyres, GT-R powertrain, brakes, aero, contact.

// 📝 The chassis, sockets, strut calibration, tyre sheet, torque curve, ratios, brake and steering limits are the
//    Project-Drive ControlVehicle's own (VehicleGeometry.h, SuspensionSpecification.h, PacejkaMagicFormula.h,
//    Drivetrain.cpp, VehicleSolver.h). What differs from Frontier is the contact: Frontier's tyre is an XPBD soft-body
//    lattice; here each corner casts its strut axis onto DriveCourse's CourseHeight() and resolves a point contact.
//    The transient slip states follow TyreSlipDynamics' relaxation-length form, integrated implicitly so the step
//    is stable at speed, and the low-speed tread damping of VehicleSolverConfiguration keeps standstill quiet.

import { TyreSlipSolver } from "./TyreSlipSolver.js";
import { SolidContinuumWheel, ShellContinuumWheel } from "./TyreContinuumElasticity.js";
import { CarcassWheel } from "./TyreCarcassProjection.js";
import { VehicleGeometry, ResolveSuspensionPair, ResolveSuspensionMountLocal, ResolveInertia, AntiRollBars,
         EngineSheet, TurbochargerSheet, TransmissionSheet, ChassisSheet } from "./VehicleSpecification.js";
import { ResolveCourseHeight, ResolveCourseNormal, SpawnPoint } from "./CourseStructure.js";
import { AddVector, SubtractVector, ScaleVector, CrossVector, DotVector, NormalizeVector, LengthVector, MultiplyAddVector,
         RotateVector, ConjugateQuaternion, MultiplyQuaternion, ConstructAxisQuaternion, IntegrateQuaternion,
         ConstructRigidTransform, ClampScalar } from "./TransformProjection.js";

const SubstepSeconds  = 1.0 / 480.0;   // [s]   fixed integration step
const MaximumSubsteps = 32;            // [-]   per advance, so a stalled tab does not spiral
const Gravity         = 9.81;          // [m/s²]
const RadiansToRpm    = 60.0 / (2.0 * Math.PI);
const CarcassStiffness = 220000.0;      // [N/m]   radial tyre stiffness in series with the strut past bump travel
const CarcassDamping   = 900.0;         // [N·s/m] carcass damping
const LoadLimit      = 24000.0;       // [N]     largest load handed to MF6.1, beyond its fitted range
const ReverseSpeedLimit = 9.0;          // [m/s]
const FrictionLimit    = 1.35;          // [-]     combined tyre force over normal load
const ObstacleReach    = 7.0;           // [m]     broadphase radius: hull extent plus a frame of travel
const LineLockGain     = 1.6;           // [-]     front brake pressure under line lock, relative to the pedal's full
const LineLockEngage   = 4.0;           // [m/s]   line lock engages below this speed …
const LineLockRelease  = 9.0;           // [m/s]   … and holds until the car creeps past this one
const LineLockHoldShare = 0.35;         // [-]     static-over-sliding friction margin of a clamped front tyre, × load
const LineLockHoldDamping = 9000.0;     // [N·s/m] how stiffly that margin resists creep
const StepUp           = 0.2;           // [m]     tallest piece of debris a tyre rolls onto

// 📝 The soft tyre in the dynamics: a carcass spring in series with the strut, its stiffness the sidewalls' plus the
//    air column's, K = K₀ + κ·p — about 223 kN/m at 220 kPa (an 18 mm static deflection under a 4 kN corner),
//    25 kN/m with no air. The deflection lowers its corner and shortens its rolling radius; past the pinch the rim
//    flange presses the road through the rubber. A flat rolled far enough sheds its rubber and the wheel runs on its
//    rim (the GI-Updated-Html-Version branch's model, carried over).
const TyreSidewallStiffness = 25000.0;     // [N/m]
const TyreAirStiffness      = 0.9;         // [N/m/Pa]
const TyrePinchClearance    = 0.022;       // [m]   rubber between flange and road at the pinch
export const TyreReferencePressure = 220000.0;    // [Pa]
const ShredDistance         = 150.0;       // [m]   rolled on a flat before the rubber comes off the rim
const ShredPressure         = 25000.0;     // [Pa]
export const RimRadiusMargin = 0.012;      // [m]   what is left of a shredded tyre on the rim
const RimGrip               = 0.32;        // [-]   a bare rim on asphalt, of the tyre's friction
const ScrapeSpeed           = 2.0;         // [m/s] sliding contact faster than this throws sparks
const RimStiffness          = 1.5e6;       // [N/m] a rim flange on the road through what rubber is left
const PinchImpactRate       = 2.6;         // [m/s] a pinch closing faster than this cuts the carcass (a snake bite)
const TyreDampingRatio      = 0.2;         // [-]   the rubber's own hysteresis, of the tyre spring on a corner's mass
const CornerMass            = 330.0;       // [kg]  a quarter of the car, for the tyre's damping

// 📝 Collision hull of the body, CoM-local spheres [x, y, z, radius]: a lower ring along the sills and an upper cabin.
export const HullSpheres = [
    [ 2.70,  0.85, 0.12, 0.32], [ 2.70, -0.85, 0.12, 0.32], [ 1.35,  0.90, 0.12, 0.32], [ 1.35, -0.90, 0.12, 0.32],
    [ 0.00,  0.90, 0.12, 0.32], [ 0.00, -0.90, 0.12, 0.32], [-1.35,  0.90, 0.12, 0.32], [-1.35, -0.90, 0.12, 0.32],
    [-2.70,  0.85, 0.18, 0.32], [-2.70, -0.85, 0.18, 0.32], [ 1.10,  0.55, 0.70, 0.42], [ 1.10, -0.55, 0.70, 0.42],
    [-0.40,  0.55, 0.80, 0.45], [-0.40, -0.55, 0.80, 0.45], [-1.80,  0.00, 0.62, 0.42],
    // 📝 Nose and tail centres: without them a narrow obstacle between the sill spheres (a crate, a post) passed
    //    under the bumper and was met only by the cabin.
    [ 2.72,  0.00, 0.30, 0.34], [-2.78,  0.00, 0.32, 0.34]
];

function SampleCurve(Curve, Abscissa)
{
    if (Abscissa <= Curve[0][0])
        return Curve[0][1];
    for (let Index = 1; Index < Curve.length; ++Index)
    {
        if (Abscissa <= Curve[Index][0])
        {
            const [X0, Y0] = Curve[Index - 1];
            const [X1, Y1] = Curve[Index];
            return Y0 + (Y1 - Y0) * (Abscissa - X0) / (X1 - X0);
        }
    }
    return Curve[Curve.length - 1][1];
}

export class VehicleSolver
{
    constructor(Obstacles)
    {
        this.Tyres         = new TyreSlipSolver();
        this.TyreModel     = "solid"; // Default: 3D Hexahedral Neo-Hookean Continuum FEM
        this.SolidWheels   = [0, 1, 2, 3].map(() => new SolidContinuumWheel());
        this.ShellWheels   = [0, 1, 2, 3].map(() => new ShellContinuumWheel());
        this.CarcassWheels = [0, 1, 2, 3].map(() => new CarcassWheel());
        const Pair         = ResolveSuspensionPair();
        this.Struts        = [Pair.Front, Pair.Front, Pair.Rear, Pair.Rear];
        this.StaticLength  = 0.6436 - 0.0914;
        this.InertiaBody   = ResolveInertia();
        this.Obstacles     = Obstacles;
        this.Accumulator   = 0.0;
        this.LineLock      = true;    // [-] assist: both pedals near standstill hold only the front brakes
        this.Burnout       = false;   // [-] line lock currently engaged
        this.Support       = null;    // [-] RigidBodyWorld: heavy debris the wheels can drive over (QuerySupport)
        this.Frozen        = false;   // [-] held where it is (rezzing in): no integration, no forces
        this.Respawn();
    }

    ResolveActiveWheelSolver(Wheel)
    {
        if (this.TyreModel === "solid") return this.SolidWheels[Wheel];
        if (this.TyreModel === "shell") return this.ShellWheels[Wheel];
        if (this.TyreModel === "xpbd")  return this.CarcassWheels[Wheel];
        return null;
    }

    AssignTyreModel(Model)
    {
        this.TyreModel = (Model === "solid" || Model === "shell" || Model === "xpbd" || Model === "spring") ? Model : "solid";
        if (this.Wheels)
        {
            for (let Wheel = 0; Wheel < 4; ++Wheel)
                this.Wheels[Wheel].Carcass = this.ResolveActiveWheelSolver(Wheel);
            this.AdvanceTyreContinuum(1.0 / 120.0);
        }
    }

    QueryCarcassPositions(WheelIndex)
    {
        return this.Wheels?.[WheelIndex]?.Carcass?.Positions || null;
    }

    Respawn(Position = [SpawnPoint.Position[0], SpawnPoint.Position[1], VehicleGeometry.CoMHeight + 0.02], Yaw = SpawnPoint.Yaw)
    {
        this.Position        = Position.slice();
        this.Orientation     = ConstructAxisQuaternion([0.0, 0.0, 1.0], Yaw);
        this.LinearVelocity  = [0.0, 0.0, 0.0];
        this.AngularVelocity = [0.0, 0.0, 0.0];
        this.Wheels = [0, 1, 2, 3].map((Wheel) => ({
            Mount:       ResolveSuspensionMountLocal(Wheel),
            Steered:     Wheel < 2,
            Driven:      Wheel >= 2,
            Length:      this.StaticLength,
            Compression: this.Struts[Wheel].FreeLength - this.StaticLength,
            Deflection:  0.0,
            Spin:        0.0,
            SpinAngle:   0.0,
            SlipRatio:   0.0,
            SlipAngle:   0.0,
            Load:        0.0,
            Contact:     false,
            Steer:       0.0,
            Skid:        0.0,
            Grip:        1.0,
            SlidingEnergy:  0.0,
            ContactSeconds: 0.0,
            SampleSeconds:  0.0,
            SlipTravel:     0.0,
            ContactPoint:    null,
            ContactVelocity: [0.0, 0.0, 0.0],
            SupportBody:     null,
            SupportPoint:    null,
            SupportImpulse:  [0.0, 0.0, 0.0],
            SupportSeconds:  0.0,
            Radius:          VehicleGeometry.TyreRadius,
            Pressure:        this.Pressures ? this.Pressures[Wheel] : TyreReferencePressure,
            TyreDeflection:  0.016,
            TyreForce:       0.0,
            DeflectionRate:  0.0,
            Pinched:         false,
            ToppedOut:       false,
            Shredded:        false,
            ShredDistance:   0.0,
            HubDistance:     Infinity,
            GroundNormal:    [0.0, 0.0, 1.0],
            LateralForce:    0.0,
            RollingSpeed:    0.0,
            Carcass:         this.ResolveActiveWheelSolver(Wheel)
        }));
        for (let Wheel = 0; Wheel < 4; ++Wheel)
        {
            const Mount = AddVector(this.Position, RotateVector(this.Orientation, this.Wheels[Wheel].Mount));
            const Down  = RotateVector(this.Orientation, [0.0, 0.0, -1.0]);
            const Hub   = MultiplyAddVector(Mount, Down, this.StaticLength);
            this.SolidWheels[Wheel].Reset(Hub, this.Orientation);
            this.ShellWheels[Wheel].Reset(Hub, this.Orientation);
            this.CarcassWheels[Wheel].Reset(Hub, this.Orientation);
        }
        this.ShredEvents  = [];
        this.PinchEvents  = [];
        this.Scrapes      = new Map();
        this.SteerAngle   = 0.0;
        this.GearIndex    = TransmissionSheet.FirstIndex;
        this.EngineRPM    = EngineSheet.IdleRPM;
        this.Boost        = 0.0;
        this.ShiftTimer   = 0.0;
        this.ReverseTimer = 0.0;
        this.Throttle     = 0.0;
        this.Accumulator  = 0.0;
        this.Burnout      = false;
        this.NearObstacles = this.ResolveNearObstacles();
    }

    // 📝 Driver assists the editor toggles: LineLock (W + S held together near standstill is a burnout).
    AssignAssists(Assists)
    {
        this.LineLock = !!Assists.LineLock;
    }

    // 📝 Static obstacle boxes (walls, tunnel, cones, enabled stress objects); the broadphase list is refreshed here.
    AssignObstacles(Obstacles)
    {
        this.Obstacles = Obstacles;
        this.NearObstacles = this.ResolveNearObstacles();
    }

    // 📝 Boxes within ObstacleReach of the centre of mass. Refreshed once per advance, it keeps the per-substep hull
    //    test independent of how many obstacles the scene holds.
    ResolveNearObstacles()
    {
        const Position = this.Position;
        return this.Obstacles.filter((Box) =>
        {
            const DeltaX = Math.max(Box.Minimum[0] - Position[0], 0.0, Position[0] - Box.Maximum[0]);
            const DeltaY = Math.max(Box.Minimum[1] - Position[1], 0.0, Position[1] - Box.Maximum[1]);
            const DeltaZ = Math.max(Box.Minimum[2] - Position[2], 0.0, Position[2] - Box.Maximum[2]);
            return DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ < ObstacleReach * ObstacleReach;
        });
    }

    // 📝 An impulse [N·s] at a world point — debris and cones push back through this (RigidBodyWorld does the same
    //    through the velocities directly while it resolves contacts).
    ApplyImpulse(Point, Impulse)
    {
        const Lever = SubtractVector(Point, this.Position);
        this.LinearVelocity = MultiplyAddVector(this.LinearVelocity, Impulse, 1.0 / VehicleGeometry.Mass);
        const Body = RotateVector(ConjugateQuaternion(this.Orientation), CrossVector(Lever, Impulse));
        this.AngularVelocity = AddVector(this.AngularVelocity, RotateVector(this.Orientation,
            [Body[0] / this.InertiaBody[0], Body[1] / this.InertiaBody[1], Body[2] / this.InertiaBody[2]]));
    }

    // 📝 The surface under a strut: the course, or a heavy body's top when one is higher (and below Ceiling). A tyre
    //    climbs what it can roll onto — a step of at most StepUp above the ground it stood on last step, or the
    //    piece it is already riding; anything taller is an obstacle the hull and wheels push against instead.
    ResolveGround(X, Y, Ceiling, Wheel = null)
    {
        const Course = ResolveCourseHeight(X, Y);
        if (this.Support && this.Support.Support.length > 0)
        {
            const Hit = this.Support.QuerySupport(X, Y, Ceiling);
            if (Hit && Hit.Height > Course)
            {
                const Riding = Wheel && Wheel.SupportBody === Hit.Body;
                const Step = Wheel ? Hit.Height - (Wheel.GroundHeight ?? Course) : 0.0;
                if (Riding || Step <= StepUp)
                    return Hit;
            }
        }
        return { Height: Course, Normal: null, Body: null };
    }

    Advance(Command, Seconds)
    {
        this.LastIntegrationSteps=0;
        if (this.Frozen)
        {
            this.Accumulator = 0.0;
            this.LinearVelocity = [0.0, 0.0, 0.0];
            this.AngularVelocity = [0.0, 0.0, 0.0];
            return;
        }
        this.NearObstacles = this.ResolveNearObstacles();
        this.Accumulator = Math.min(this.Accumulator + Math.max(0.0, Seconds), SubstepSeconds * MaximumSubsteps);
        while (this.Accumulator >= SubstepSeconds)
        {
            this.LastIntegrationSteps++;
            this.Step(Command, SubstepSeconds);
            this.Accumulator -= SubstepSeconds;
        }
        this.AdvanceTyreContinuum(Math.max(SubstepSeconds, Math.min(Seconds || (1.0 / 60.0), 1.0 / 30.0)));
    }

    AdvanceTyreContinuum(dt)
    {
        if (this.TyreModel === "spring")
        {
            for (let Index = 0; Index < 4; ++Index)
                this.Wheels[Index].Carcass = null;
            return;
        }
        const DownBody = RotateVector(this.Orientation, [0.0, 0.0, -1.0]);
        for (let Index = 0; Index < 4; ++Index)
        {
            const Wheel  = this.Wheels[Index];
            const Solver = this.ResolveActiveWheelSolver(Index);
            Wheel.Carcass = Solver;
            if (!Solver || Wheel.Shredded) continue;

            const Mount   = AddVector(this.Position, RotateVector(this.Orientation, Wheel.Mount));
            const Hub     = MultiplyAddVector(Mount, DownBody, Wheel.Length);
            const Steered = MultiplyQuaternion(this.Orientation, ConstructAxisQuaternion([0.0, 0.0, 1.0], Wheel.Steer || 0.0));
            const Spun    = MultiplyQuaternion(Steered, ConstructAxisQuaternion([0.0, 1.0, 0.0], Wheel.SpinAngle || 0.0));

            Solver.Step({
                Center:       Hub,
                Orientation:  Spun,
                GroundNormal: Wheel.GroundNormal || [0.0, 0.0, 1.0],
                Deflection:   Wheel.Contact ? Wheel.TyreDeflection : 0.0,
                Pressure:     Wheel.Pressure,
                LateralForce: Wheel.LateralForce || 0.0
            }, dt, 2);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  DRIVETRAIN
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 GT-R curve × first-order turbo boost × throttle, through an AMT that slips toward LaunchRPM until the wheels
    //    can carry the engine, auto-shifting at 6800 / 2600 rpm with a 0.35 s dwell and a short torque cut per shift.
    ResolveDriveTorque(Throttle, ForwardSpeed, Δτ)
    {
        const Ratio = TransmissionSheet.GearRatios[this.GearIndex] * TransmissionSheet.FinalDriveRatio;
        const DrivenSpin = 0.5 * (this.Wheels[2].Spin + this.Wheels[3].Spin);
        const WheelRPM = Math.abs(DrivenSpin * Ratio) * RadiansToRpm;
        const Launch = EngineSheet.IdleRPM + (TransmissionSheet.LaunchRPM - EngineSheet.IdleRPM) * Throttle;
        // 📝 Wheels turning against the selected gear — rolling back in a forward gear, or forward in reverse — would drive
        //    the engine backwards: the AMT's clutch is open, so the engine neither brakes them (its braking torque, signed
        //    for the gear's own direction, would otherwise speed them up — a runaway on a low-grip rim) nor drives them
        //    until the pedal slips the clutch in against the roll.
        const Against = DrivenSpin * Ratio < -1.0e-3;
        this.EngineRPM = Against ? (Throttle > 0.02 ? Launch : EngineSheet.IdleRPM) : Math.max(WheelRPM, Throttle > 0.02 ? Launch : EngineSheet.IdleRPM);
        // 📝 Below idle with the pedal up the AMT clutch is open: the engine idles and drives nothing.
        if (Throttle <= 0.02 && (WheelRPM < EngineSheet.IdleRPM * 1.2 || Against))
        {
            this.ShiftTimer = Math.max(0.0, this.ShiftTimer - Δτ);
            this.Boost += (0.0 - this.Boost) * Math.min(1.0, Δτ / TurbochargerSheet.SpoolTimeConstant);
            return 0.0;
        }
        const BoostTarget = Throttle * TurbochargerSheet.WastegateMaxBoost * ClampScalar((this.EngineRPM - 1500.0) / 2500.0, 0.0, 1.0);
        this.Boost += (BoostTarget - this.Boost) * Math.min(1.0, Δτ / TurbochargerSheet.SpoolTimeConstant);
        const BoostMultiplier = SampleCurve(TurbochargerSheet.TorqueMultiplierCurve, this.Boost);
        const ω = this.EngineRPM / RadiansToRpm;
        let Torque = SampleCurve(EngineSheet.TorqueCurve, this.EngineRPM) * BoostMultiplier * Throttle;
        Torque -= (EngineSheet.EngineBrakingCoeff * ω + EngineSheet.EngineBrakingQuadratic * ω * ω * (1.0 - Throttle));
        if (this.EngineRPM >= EngineSheet.RedlineRPM)
            Torque = Math.min(Torque, 0.0);
        if ((WheelRPM < Launch || Against) && Throttle > 0.02)
            Torque = Math.min(Math.max(Torque, 0.0), TransmissionSheet.ClutchCapacity);
        this.ShiftTimer = Math.max(0.0, this.ShiftTimer - Δτ);
        if (this.ShiftTimer > TransmissionSheet.ShiftCooldownSeconds - 0.12)
            Torque = Math.min(Torque, 0.0);
        // 📝 VehicleSolver.cpp's AMT: upshift on rpm only under throttle, downshift on rpm at any speed, and back to
        //    first once nearly stopped off the throttle. (A speed floor on the downshift used to strand the car in a
        //    high gear after wheelspin had walked it up the box during a spin.)
        if (this.GearIndex >= TransmissionSheet.FirstIndex && this.ShiftTimer <= 0.0)
        {
            const Top = TransmissionSheet.GearRatios.length - 1;
            if (WheelRPM > TransmissionSheet.UpshiftRPM && this.GearIndex < Top && Throttle > 0.1 && !this.Burnout)
            {
                this.GearIndex++;
                this.ShiftTimer = TransmissionSheet.ShiftCooldownSeconds;
            }
            else if (WheelRPM < TransmissionSheet.DownshiftRPM && this.GearIndex > TransmissionSheet.FirstIndex)
            {
                this.GearIndex--;
                this.ShiftTimer = TransmissionSheet.ShiftCooldownSeconds;
            }
        }
        if (this.GearIndex > TransmissionSheet.FirstIndex && Math.abs(ForwardSpeed) < 0.5 && Throttle < 0.05)
            this.GearIndex = TransmissionSheet.FirstIndex;
        return Torque * Ratio * TransmissionSheet.DriveEfficiency;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 INTEGRATION
    //--------------------------------------------------------------------------------------------------------------------

    Step(Command, Δτ)
    {
        const Orientation = this.Orientation;
        const Forward = RotateVector(Orientation, [1.0, 0.0, 0.0]);
        const Upward  = RotateVector(Orientation, [0.0, 0.0, 1.0]);
        const ForwardSpeed = DotVector(this.LinearVelocity, Forward);
        const Speed = LengthVector(this.LinearVelocity);

        // ① pedals, with S as reverse once the car has stopped and W as the brake while reversing
        let Throttle = Command.Throttle, Brake = Command.Brake;
        const Reversing = this.GearIndex === TransmissionSheet.ReverseIndex;
        if (!Reversing && Command.Brake > 0.5 && Command.Throttle < 0.05 && Math.abs(ForwardSpeed) < 0.6)
        {
            this.ReverseTimer += Δτ;
            if (this.ReverseTimer > 0.25)
                this.GearIndex = TransmissionSheet.ReverseIndex;
        }
        else if (Reversing && Command.Throttle > 0.5 && Math.abs(ForwardSpeed) < 0.6)
        {
            this.GearIndex = TransmissionSheet.FirstIndex;
            this.ReverseTimer = 0.0;
        }
        else if (Command.Brake < 0.1)
        {
            this.ReverseTimer = 0.0;
        }
        if (this.GearIndex === TransmissionSheet.ReverseIndex)
        {
            Throttle = Command.Brake * 0.6 * ClampScalar((ReverseSpeedLimit + ForwardSpeed) / 3.0, 0.0, 1.0);
            Brake = Command.Throttle;
        }
        this.Throttle = Throttle;
        // 📝 Line lock — the drag-strip burnout valve: with both pedals down at a crawl in a forward gear only the front
        //    calipers clamp (harder than the pedal alone would), the rears are free to spin against them, and the AMT
        //    holds its gear so the engine sits on the limiter. Lifting the brake launches the car.
        const Crawl = Math.abs(ForwardSpeed) < (this.Burnout ? LineLockRelease : LineLockEngage);
        this.Burnout = this.LineLock && this.GearIndex !== TransmissionSheet.ReverseIndex
                    && Command.Throttle > 0.4 && Command.Brake > 0.4 && Crawl;

        // ② steering: first-order toward the command, the lock eased with speed for keyboard driving, Ackermann split
        const Lock = ChassisSheet.MaxSteerAngleRad / (1.0 + Math.max(0.0, Speed - 8.0) / 22.0);
        this.SteerAngle += (Command.Steer * Lock - this.SteerAngle) * Math.min(1.0, ChassisSheet.SteerRatePerSecond * Δτ);
        const Wheelbase = 3.396, Track = 2.095;
        const Tangent = Math.tan(this.SteerAngle);
        const InnerOuter = (Sign) => Math.atan(Wheelbase * Tangent / (Wheelbase - Sign * 0.5 * Track * Tangent));
        this.Wheels[0].Steer = Math.abs(Tangent) > 1.0e-5 ? InnerOuter(1.0) : 0.0;
        this.Wheels[1].Steer = Math.abs(Tangent) > 1.0e-5 ? InnerOuter(-1.0) : 0.0;

        const DriveTorque = this.ResolveDriveTorque(Throttle, ForwardSpeed, Δτ);
        const Differential = 40.0 * (this.Wheels[2].Spin - this.Wheels[3].Spin);
        const DriveSplit = [0.0, 0.0,
                            0.5 * DriveTorque - ClampScalar(Differential, -0.25 * Math.abs(DriveTorque) - 20.0, 0.25 * Math.abs(DriveTorque) + 20.0),
                            0.5 * DriveTorque + ClampScalar(Differential, -0.25 * Math.abs(DriveTorque) - 20.0, 0.25 * Math.abs(DriveTorque) + 20.0)];

        let Force  = [0.0, 0.0, -VehicleGeometry.Mass * Gravity];
        let Torque = [0.0, 0.0, 0.0];
        const ApplyForce = (Applied, Point) =>
        {
            Force = AddVector(Force, Applied);
            Torque = AddVector(Torque, CrossVector(SubtractVector(Point, this.Position), Applied));
        };

        // ③ struts: cast each strut axis onto the course (or debris), then solve each axle's two strut–tyre stacks with
        //    the anti-roll bar between them
        const Axis = ScaleVector(Upward, -1.0);
        const Strut = [];
        const Stacks = [];
        for (let Index = 0; Index < 4; ++Index)
        {
            const Wheel = this.Wheels[Index];
            const Specification = this.Struts[Index];
            const Mount = AddVector(this.Position, RotateVector(Orientation, Wheel.Mount));
            const MaximumLength = this.StaticLength + Specification.DroopTravel;
            const BumpLength = this.StaticLength - Specification.BumpTravel;
            const Stack = { Mount, Normal: [0.0, 0.0, 1.0], Contact: false, MaximumLength, BumpLength, Length: MaximumLength,
                            TouchLength: Infinity, Projection: 1.0 };
            const Riding = Wheel.SupportBody;
            if (Axis[2] < -0.25)
            {
                // 📝 The strut casts onto the course — or onto a heavy piece of debris lying on it (ResolveGround).
                let Surface = this.ResolveGround(Mount[0], Mount[1], Mount[2], Wheel);
                let Travel = (Mount[2] - Surface.Height) / -Axis[2];
                for (let Iteration = 0; Iteration < 3; ++Iteration)
                {
                    const Footing = MultiplyAddVector(Mount, Axis, Travel);
                    Surface = this.ResolveGround(Footing[0], Footing[1], Mount[2], Wheel);
                    Travel = (Mount[2] - Surface.Height) / -Axis[2];
                }
                Wheel.GroundHeight = Surface.Height;
                Wheel.SupportBody = null;
                const Ground = MultiplyAddVector(Mount, Axis, Travel);
                Stack.Normal = Surface.Body ? Surface.Normal : ResolveCourseNormal(Ground[0], Ground[1]);
                if (Surface.Body)
                {
                    Wheel.SupportBody = Surface.Body;
                    Wheel.SupportPoint = Ground;
                }
                // 📝 The carcass is a circle, not a point: the effective ground is the highest course point under the
                //    tyre's profile across ±0.24 m of footprint and ±0.15 m of tread, so a kerb face or the ramp lip
                //    is met by the tyre's leading arc rather than as a step under the hub.
                const Heading = RotateVector(MultiplyQuaternion(Orientation, ConstructAxisQuaternion([0.0, 0.0, 1.0], Wheel.Steer)), [1.0, 0.0, 0.0]);
                const Planar = NormalizeVector([Heading[0], Heading[1], 0.0]);
                const Across = [-Planar[1], Planar[0]];
                let Highest = -Infinity;
                for (let Along = -2; Along <= 2; ++Along)
                {
                    const Offset = Along * 0.12;
                    const Drop = Wheel.Radius - Math.sqrt(Math.max(0.0, Wheel.Radius ** 2 - Offset * Offset));
                    for (let Side = -1; Side <= 1; ++Side)
                    {
                        const Height = this.ResolveGround(Ground[0] + Planar[0] * Offset + Across[0] * Side * 0.15,
                                                          Ground[1] + Planar[1] * Offset + Across[1] * Side * 0.15, Mount[2],
                                                          Riding ? { SupportBody: Riding, GroundHeight: Wheel.GroundHeight } : Wheel).Height;
                        Highest = Math.max(Highest, Height - Drop);
                    }
                }
                const Lift = Math.max(0.0, Highest - Ground[2]) / -Axis[2];
                // 📝 TouchLength is the strut length at which the uncompressed tyre just meets the road; a longer strut
                //    squeezes the tyre by the difference (× Projection, the road normal's share along the strut).
                Stack.Projection = Math.max(Stack.Normal[2], 0.3) * -Axis[2];
                Stack.TouchLength = Travel - Lift - Wheel.Radius / Stack.Projection;
                if (Stack.TouchLength < MaximumLength)
                {
                    Stack.Contact = true;
                    Stack.Stiffness = Wheel.Shredded ? RimStiffness : TyreSidewallStiffness + TyreAirStiffness * Math.max(0.0, Wheel.Pressure);
                    Stack.Pinch = Wheel.Radius - VehicleGeometry.TyreRimRadius - TyrePinchClearance;
                }
                else
                    Wheel.SupportBody = null;
            }
            else
            {
                Wheel.SupportBody = null;
                Wheel.GroundHeight = undefined;
            }
            Stacks.push(Stack);
        }

        // 📝 The strut and the carcass in series: per corner the strut length where the strut's push (spring, bump
        //    carcass and the bar's share) equals the squeezed tyre's, found by bisection — both sides are monotone in
        //    the length. A strut that tops out first leaves the corner carried by whatever the squeezed tyre pushes, so
        //    a flat corner sinks until its deflated tyre holds it, and past the pinch the rim flange stiffens the
        //    stack. The bar couples an axle's two corners (k·(c_left − c_right) on the left, its negative on the
        //    right); three Gauss–Seidel passes over the pair solve the coupled balance implicitly, so the tyre's
        //    deflection carries exactly the load the corner bears — a flat corner keeps its share instead of the bar
        //    lifting it off the road.
        const TyreForceOf = (Index, Candidate) =>
        {
            const Stack = Stacks[Index], Wheel = this.Wheels[Index];
            const Squeeze = Math.max(0.0, (Candidate - Stack.TouchLength) * Stack.Projection);
            return Stack.Stiffness * Squeeze + (Wheel.Shredded ? 0.0 : RimStiffness * Math.max(0.0, Squeeze - Stack.Pinch));
        };
        const CompressionOf = (Index, Candidate) => this.Struts[Index].FreeLength - Math.max(Candidate, Stacks[Index].BumpLength);
        const StrutForceOf = (Index, Candidate, BarStiffness, OtherCompression) =>
        {
            const Specification = this.Struts[Index];
            const Compressed = CompressionOf(Index, Candidate);
            const Spring = (Compressed > 0.0 ? Specification.Preload + Specification.SpringForce(Compressed) : 0.0)
                         + CarcassStiffness * Math.max(0.0, Stacks[Index].BumpLength - Candidate);
            // 📝 The bar resists the pair's difference: k·(own − partner) on each side, so the stiffer the bar the more
            //    a corner compressing alone pushes back — on the right exactly as on the left.
            return Spring + BarStiffness * (Compressed - OtherCompression);
        };
        const SolveCorner = (Index, BarStiffness, OtherCompression) =>
        {
            const Stack = Stacks[Index];
            let Low = Stack.BumpLength - 0.12, High = Stack.MaximumLength;
            if (StrutForceOf(Index, High, BarStiffness, OtherCompression) >= TyreForceOf(Index, High))
            {
                Stack.Length = High;
                Stack.ToppedOut = true;
                return;
            }
            // 📝 The longest length the balance allows: where neither side pushes — the spring off its seat past its
            //    free length, the tyre not yet touching — a wheel hangs down to the road (or to full droop) rather than
            //    hovering at the free length and slamming onto the road the moment the body sinks a millimetre.
            for (let Halving = 0; Halving < 16; ++Halving)
            {
                const Middle = 0.5 * (Low + High);
                if (StrutForceOf(Index, Middle, BarStiffness, OtherCompression) >= TyreForceOf(Index, Middle))
                    Low = Middle;
                else
                    High = Middle;
            }
            Stack.Length = 0.5 * (Low + High);
            Stack.ToppedOut = false;
            Stack.Pinned = Stack.Length < Stack.BumpLength - 0.12 + 1.0e-3;
        };
        for (const Bar of [AntiRollBars.Front, AntiRollBars.Rear])
        {
            const Left = Bar.LeftWheel, Right = Bar.RightWheel;
            const Coupled = Stacks[Left].Contact && Stacks[Right].Contact;
            const Stiffness = Coupled ? Bar.Stiffness : 0.0;
            Stacks[Left].Length = Stacks[Left].Contact ? Math.min(this.Wheels[Left].Length, Stacks[Left].MaximumLength) : Stacks[Left].MaximumLength;
            Stacks[Right].Length = Stacks[Right].Contact ? Math.min(this.Wheels[Right].Length, Stacks[Right].MaximumLength) : Stacks[Right].MaximumLength;
            for (let Pass = 0; Pass < (Coupled ? 3 : 1); ++Pass)
            {
                if (Stacks[Left].Contact)
                    SolveCorner(Left, Stiffness, CompressionOf(Right, Stacks[Right].Length));
                if (Stacks[Right].Contact)
                    SolveCorner(Right, Stiffness, CompressionOf(Left, Stacks[Left].Length));
            }
            Stacks[Left].BarStiffness = Stiffness;
            Stacks[Right].BarStiffness = Stiffness;
            Stacks[Left].Partner = Right;
            Stacks[Right].Partner = Left;
            Stacks[Left].BarDamping = Coupled ? Bar.Damping : 0.0;
            Stacks[Right].BarDamping = Coupled ? Bar.Damping : 0.0;
        }

        for (let Index = 0; Index < 4; ++Index)
        {
            const Wheel = this.Wheels[Index];
            const Specification = this.Struts[Index];
            const Stack = Stacks[Index];
            const Contact = Stack.Contact;
            const Length = Stack.Length;
            if (Contact)
            {
                const TyreDeflection = Math.max(0.0, (Length - Stack.TouchLength) * Stack.Projection);
                const Closing = (TyreDeflection - Wheel.TyreDeflection) / Δτ;
                const WasPinched = Wheel.Pinched;
                Wheel.TyreForce = TyreForceOf(Index, Length);
                Wheel.TyreDeflection = TyreDeflection;
                Wheel.DeflectionRate = Closing;
                Wheel.ToppedOut = !!Stack.ToppedOut;
                Wheel.Pinched = !Wheel.Shredded && TyreDeflection > Stack.Pinch;
                // 📝 A pinch that closes hard — a kerb or a landing driving the rim through the rubber — cuts the
                //    carcass between flange and road: the host turns it into a puncture. A solve pinned at its lower
                //    bound is the strut meeting a step face (the ramp's end, a kerb), not a tyre being crushed, and a
                //    closing faster than any landing is the same artefact.
                if (Wheel.Pinched && !WasPinched && !Stack.Pinned && Closing > PinchImpactRate && Closing < 14.0 && Wheel.Pressure > ShredPressure)
                    this.PinchEvents.push({ Wheel: Index, Rate: Closing });
            }
            else
            {
                Wheel.TyreDeflection = 0.0;
                Wheel.TyreForce = 0.0;
                Wheel.Pinched = false;
                Wheel.ToppedOut = false;
            }
            // 📝 Past the strut's bump travel the remaining closure is carried by the tyre carcass in series — the
            //    compliance Frontier's soft tyre lattice supplies — rather than by an ideal stop.
            const Deflection = Math.max(0.0, Stack.BumpLength - Length);
            const StrutLength = Math.max(Length, Stack.BumpLength);
            const Compression = Specification.FreeLength - StrutLength;
            const Rate = ClampScalar((Compression - Wheel.Compression) / Δτ, -4.0, 4.0);
            const DeflectionRate = ClampScalar((Deflection - Wheel.Deflection) / Δτ, -3.0, 3.0);
            Wheel.Compression = Compression;
            Wheel.Deflection = Deflection;
            Wheel.Length = StrutLength;
            Wheel.Contact = Contact;
            Wheel.Rate = Rate;
            // 📝 The load the corner puts through its tyre: the balanced static push (spring, bump carcass, bar) plus
            //    the strut's and the bar's damping and the carcass damping past bump travel.
            let Load = 0.0;
            if (Contact && Wheel.ToppedOut)
            {
                // 📝 A topped-out strut leaves the corner on the tyre alone — the rubber's own damping, or the corner
                //    hops on an undamped 223 kN/m spring.
                const Stiffness = Stacks[Index].Stiffness || TyreSidewallStiffness;
                const TyreDamping = 2.0 * TyreDampingRatio * Math.sqrt(Stiffness * CornerMass);
                Load = Math.max(0.0, Wheel.TyreForce + TyreDamping * ClampScalar(Wheel.DeflectionRate, -3.0, 3.0));
            }
            else if (Contact)
                Load = Wheel.TyreForce + Specification.DampingRate * Rate + (Deflection > 0.0 ? CarcassDamping * DeflectionRate : 0.0);
            Strut.push({ Mount: Stack.Mount, Load, Rate, Normal: Stack.Normal });
        }
        // 📝 The bars' damping: on the relative rate of the pair, moved across the axle like the bars' stiffness.
        for (const Bar of [AntiRollBars.Front, AntiRollBars.Rear])
        {
            if (!Stacks[Bar.LeftWheel].Contact || !Stacks[Bar.RightWheel].Contact)
                continue;
            const Damping = Bar.Damping * (Strut[Bar.LeftWheel].Rate - Strut[Bar.RightWheel].Rate);
            const Coupling = ClampScalar(Damping, -Math.max(0.0, Strut[Bar.LeftWheel].Load), Math.max(0.0, Strut[Bar.RightWheel].Load));
            Strut[Bar.LeftWheel].Load += Coupling;
            Strut[Bar.RightWheel].Load -= Coupling;
        }

        // ④ tyres: transient slip into MF6.1, low-speed tread damping, wheel spin against drive and brake torque
        const AeroSquared = ForwardSpeed * ForwardSpeed;
        const Downforce = 0.5 * ChassisSheet.AirDensity * ChassisSheet.LiftArea * AeroSquared;
        for (let Index = 0; Index < 4; ++Index)
        {
            const Wheel = this.Wheels[Index];
            const Hub = MultiplyAddVector(Strut[Index].Mount, Axis, Wheel.Length);
            Wheel.Load = Math.max(0.0, Strut[Index].Load);
            if (Wheel.Load > 0.0)
                ApplyForce(ScaleVector(Upward, Wheel.Load), Hub);

            const Inertia = ChassisSheet.WheelInertia;
            let BrakeTorque = Brake * ChassisSheet.MaxBrakeTorquePerWheel;
            if (this.Burnout)
                BrakeTorque = Wheel.Steered ? Brake * ChassisSheet.MaxBrakeTorquePerWheel * LineLockGain : 0.0;
            if (Command.Handbrake && !Wheel.Steered)
                BrakeTorque += ChassisSheet.HandbrakeTorque;
            let SpinTorque = DriveSplit[Index];
            Wheel.Skid = 0.0;

            if (Wheel.Contact && Wheel.Load > 0.0)
            {
                const Normal = Strut[Index].Normal;
                const Heading = RotateVector(MultiplyQuaternion(Orientation, ConstructAxisQuaternion([0.0, 0.0, 1.0], Wheel.Steer)), [1.0, 0.0, 0.0]);
                const Longitudinal = NormalizeVector(SubtractVector(Heading, ScaleVector(Normal, DotVector(Heading, Normal))));
                const Lateral = CrossVector(Normal, Longitudinal);
                const Contact = MultiplyAddVector(Hub, Normal, -(Wheel.Radius - Wheel.TyreDeflection));
                const Velocity = AddVector(this.LinearVelocity, CrossVector(this.AngularVelocity, SubtractVector(Contact, this.Position)));
                const Vx = DotVector(Velocity, Longitudinal);
                const Vy = DotVector(Velocity, Lateral);
                const RollingRadius = Math.max(VehicleGeometry.TyreRimRadius, Wheel.Radius - Wheel.TyreDeflection / 3.0 - (Wheel.Shredded ? 0.0 : 0.012));
                const SlipSpeed = Vx - Wheel.Spin * RollingRadius;
                const [σx, σy] = this.Tyres.ResolveRelaxationLengths(Wheel.Load);
                // 📐 σ·dκ'/dt + |Vx|·κ' = −Vsx and σ·dα'/dt + |Vx|·α' = −Vy, integrated backward-Euler.
                Wheel.SlipRatio = ClampScalar((Wheel.SlipRatio - SlipSpeed * Δτ / σx) / (1.0 + Math.abs(Vx) * Δτ / σx), -1.5, 1.5);
                Wheel.SlipAngle = ClampScalar((Wheel.SlipAngle - Vy * Δτ / σy) / (1.0 + Math.abs(Vx) * Δτ / σy), -1.3, 1.3);
                const AxleLoad = Math.min(LoadLimit, Wheel.Load
                               + Downforce * (Wheel.Steered ? ChassisSheet.FrontLiftFraction : 1.0 - ChassisSheet.FrontLiftFraction) * 0.5);
                let [Fx, Fy] = this.Tyres.Combine(Wheel.SlipRatio, Wheel.SlipAngle, 0.0, AxleLoad);
                // 📝 Grip is 1 unless thermal grip is enabled: then the tyre's own condition multiplier (hot tread,
                //    pressure away from cold) scales this wheel's force and its friction limit, and no other wheel's.
                Fx *= Wheel.Grip;
                Fy *= Wheel.Grip;
                // 📝 An underinflated tyre grips less, a pinched one less again, a bare rim least of all.
                const PressureGrip = Wheel.Shredded ? RimGrip
                                   : (0.62 + 0.38 * Math.min(1.0, Math.pow(Math.max(0.0, Wheel.Pressure / (0.75 * TyreReferencePressure)), 0.7)))
                                     * (Wheel.Pinched ? 0.7 : 1.0);
                Fx *= PressureGrip;
                Fy *= PressureGrip;
                const Fade = Math.max(0.0, 1.0 - Math.abs(Vx) / ChassisSheet.ContactDampingFadeSpeed);
                Fx -= ChassisSheet.SpinDampingCoefficient * SlipSpeed * Fade;
                Fy -= ChassisSheet.LateralDampingCoefficient * Vy * Fade;
                // 📝 GRIT's sheet peaks above μ 2.3 laterally (pDy1 · Ly), past the chassis's 1.98 g static rollover
                //    threshold; VehicleGeometry.h states the intent that the car slides before it rolls, so the combined
                //    force is held to FrictionLimit · Fz.
                const Limit = FrictionLimit * AxleLoad * Wheel.Grip * PressureGrip;
                const Magnitude = Math.hypot(Fx, Fy);
                if (Magnitude > Limit)
                {
                    Fx *= Limit / Magnitude;
                    Fy *= Limit / Magnitude;
                }
                ApplyForce(AddVector(ScaleVector(Longitudinal, Fx), ScaleVector(Lateral, Fy)), Contact);
                // 📝 Riding on debris: the tyre's load and traction act on the piece, equal and opposite.
                if (Wheel.SupportBody)
                {
                    const Reaction = AddVector(ScaleVector(Normal, -Wheel.Load), AddVector(ScaleVector(Longitudinal, -Fx), ScaleVector(Lateral, -Fy)));
                    Wheel.SupportImpulse = MultiplyAddVector(Wheel.SupportImpulse, Reaction, Δτ);
                    Wheel.SupportSeconds += Δτ;
                }
                // 📝 A clamped front tyre under line lock is stuck, not sliding: its static friction exceeds the sliding
                //    value the friction limit above allows a locked wheel by about a third — the margin that keeps a
                //    burnout in place. It resists creep only and does no sliding work.
                if (this.Burnout && Wheel.Steered)
                {
                    const Hold = ClampScalar(Vx * LineLockHoldDamping, -LineLockHoldShare * AxleLoad, LineLockHoldShare * AxleLoad) * Wheel.Grip;
                    ApplyForce(ScaleVector(Longitudinal, -Hold), Contact);
                }
                // 📐 Sliding dissipation: the applied tangential force against the patch's slip velocity, longitudinal
                //    (Vx − ω·r) and lateral (Vy). Static support does no work and never heats the tyre.
                Wheel.SlidingEnergy += (Math.abs(Fx * SlipSpeed) + Math.abs(Fy * Vy)) * Δτ;
                Wheel.SlipTravel += Math.hypot(SlipSpeed, Vy) * Δτ;
                Wheel.ContactSeconds += Δτ;
                Wheel.ContactPoint = Contact;
                Wheel.ContactVelocity = Velocity;
                // 📝 The tread's own velocity over the ground at the patch (contact velocity less the spin): what a
                //    sliding tyre drags its smoke along with — straight back off a burnout, sideways out of a drift.
                Wheel.TreadVelocity = AddVector(ScaleVector(Longitudinal, SlipSpeed), ScaleVector(Lateral, Vy));
                SpinTorque -= Fx * RollingRadius;
                // 📝 A soft tyre's hysteresis grows with its deflection: a flat rolls at three to four times the
                //    resistance, a rim grinding the road at more again.
                const Softness = Math.max(0.0, 1.0 - Math.max(0.0, Wheel.Pressure) / TyreReferencePressure);
                const Resistance = Wheel.Shredded ? 4.0 : 1.0 + 3.0 * Softness * Softness;
                SpinTorque -= ChassisSheet.RollingResistance * Resistance * Wheel.Load * RollingRadius * Math.sign(Wheel.Spin);
                Wheel.Skid = Math.min(1.0, Math.hypot(Wheel.SlipRatio * 2.0, Wheel.SlipAngle * 3.0));
                Wheel.LateralForce = Fy;
            }
            else
                Wheel.LateralForce = 0.0;
            Wheel.GroundNormal = Strut[Index].Normal;
            Wheel.HubDistance = Wheel.Contact ? Wheel.Radius - Wheel.TyreDeflection : Infinity;
            Wheel.RollingSpeed = Math.abs(Wheel.Spin) * Wheel.Radius;
            Wheel.SampleSeconds += Δτ;
            Wheel.Spin += SpinTorque * Δτ / Inertia;
            const BrakeStep = BrakeTorque * Δτ / Inertia;
            Wheel.Spin = Math.abs(Wheel.Spin) <= BrakeStep ? 0.0 : Wheel.Spin - Math.sign(Wheel.Spin) * BrakeStep;
            Wheel.SpinAngle = (Wheel.SpinAngle + Wheel.Spin * Δτ) % (2.0 * Math.PI);
        }

        // ⑤ aerodynamics: drag at the centre of mass, downforce at the axles
        const Drag = ScaleVector(this.LinearVelocity, -0.5 * ChassisSheet.AirDensity * ChassisSheet.DragArea * Speed);
        Force = AddVector(Force, Drag);

        // ⑥ chassis integration (semi-implicit), inertia carried into the world frame
        const Acceleration = ScaleVector(Force, 1.0 / VehicleGeometry.Mass);
        this.LinearVelocity = MultiplyAddVector(this.LinearVelocity, Acceleration, Δτ);
        const BodyTorque = RotateVector(ConjugateQuaternion(Orientation), Torque);
        const BodyAngular = RotateVector(ConjugateQuaternion(Orientation), this.AngularVelocity);
        const Momentum = [this.InertiaBody[0] * BodyAngular[0], this.InertiaBody[1] * BodyAngular[1], this.InertiaBody[2] * BodyAngular[2]];
        const Gyroscopic = CrossVector(BodyAngular, Momentum);
        const AngularAcceleration = [(BodyTorque[0] - Gyroscopic[0]) / this.InertiaBody[0],
                                     (BodyTorque[1] - Gyroscopic[1]) / this.InertiaBody[1],
                                     (BodyTorque[2] - Gyroscopic[2]) / this.InertiaBody[2]];
        this.AngularVelocity = AddVector(this.AngularVelocity, RotateVector(Orientation, ScaleVector(AngularAcceleration, Δτ)));
        this.Position = MultiplyAddVector(this.Position, this.LinearVelocity, Δτ);
        this.Orientation = IntegrateQuaternion(this.Orientation, this.AngularVelocity, Δτ);
        this.ResolveHullContacts();
        // 📝 An overturned or lost car is RecoveryDirector's business now: it derezzes the car and rezzes it back.
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 HULL CONTACT
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 The body's own hull (HullStructure.js): ~700 points on its underside, top and flanks. Assigned once at bring-up;
    //    without it the sphere hull below stands in.
    AssignHullPoints(Points, Radius)
    {
        this.HullPoints = Points.map((Point) => Point.slice());
        this.HullPointRadius = Radius;
        this.HullReach = Math.max(...Points.map((Point) => Math.hypot(Point[0], Point[1], Point[2]))) + Radius;
    }

    // 📝 Point hull against the course and the obstacle boxes. Per surface the deepest point projects the body out;
    //    then every penetrating point that still approaches takes a restitution-and-friction impulse, in turn, so a
    //    car lying on its roof settles on the points that carry it rather than bouncing on all of them at once. A
    //    point sliding faster than ScrapeSpeed scrapes — the sparks start there.
    ResolveHullPoints(InverseInertiaWorld)
    {
        const Radius = this.HullPointRadius;
        const Upright = RotateVector(this.Orientation, [0.0, 0.0, 1.0])[2] > 0.65;
        const Contacts = [];
        const Boxes = (this.NearObstacles || this.Obstacles).filter((Box) =>
        {
            const P = this.Position, Reach = this.HullReach;
            return P[0] + Reach > Box.Minimum[0] && P[0] - Reach < Box.Maximum[0] && P[1] + Reach > Box.Minimum[1] && P[1] - Reach < Box.Maximum[1]
                && P[2] + Reach > Box.Minimum[2] && P[2] - Reach < Box.Maximum[2];
        });
        const GroundNear = ResolveCourseHeight(this.Position[0], this.Position[1]);
        const MayTouchGround = this.Position[2] - this.HullReach < GroundNear + 2.5;
        for (let Index = 0; Index < this.HullPoints.length; ++Index)
        {
            const Local = this.HullPoints[Index];
            // 📝 Upright, only the lower half of the hull can meet the road.
            if (Upright && Boxes.length === 0 && Local[2] > 0.15)
                continue;
            const Point = AddVector(this.Position, RotateVector(this.Orientation, Local));
            let Normal = null, Depth = 0.0, Surface = -1;
            if (MayTouchGround)
            {
                const Ground = ResolveCourseHeight(Point[0], Point[1]);
                if (Point[2] - Radius < Ground)
                {
                    Normal = ResolveCourseNormal(Point[0], Point[1]);
                    Depth = (Ground + Radius - Point[2]) * Normal[2];
                    Surface = -1;
                }
            }
            for (let BoxIndex = 0; BoxIndex < Boxes.length; ++BoxIndex)
            {
                const Box = Boxes[BoxIndex];
                if (Point[0] < Box.Minimum[0] - Radius || Point[0] > Box.Maximum[0] + Radius || Point[1] < Box.Minimum[1] - Radius
                 || Point[1] > Box.Maximum[1] + Radius || Point[2] < Box.Minimum[2] - Radius || Point[2] > Box.Maximum[2] + Radius)
                    continue;
                const Exits = [Point[0] - Box.Minimum[0] + Radius, Box.Maximum[0] - Point[0] + Radius, Point[1] - Box.Minimum[1] + Radius,
                               Box.Maximum[1] - Point[1] + Radius, Point[2] - Box.Minimum[2] + Radius, Box.Maximum[2] - Point[2] + Radius];
                let Face = 0;
                for (let Candidate = 1; Candidate < 6; ++Candidate)
                    if (Exits[Candidate] < Exits[Face])
                        Face = Candidate;
                if (Exits[Face] > Depth)
                {
                    Normal = [0.0, 0.0, 0.0];
                    Normal[Face >> 1] = (Face & 1) ? 1.0 : -1.0;
                    Depth = Exits[Face];
                    Surface = BoxIndex;
                }
            }
            if (Normal)
                Contacts.push({ Index, Local, Normal, Depth, Surface });
        }
        this.HullContacts = Contacts.length;
        if (Contacts.length === 0)
            return;
        // ① projection: the deepest contact of each surface
        const Deepest = new Map();
        for (const Contact of Contacts)
            if (!Deepest.has(Contact.Surface) || Deepest.get(Contact.Surface).Depth < Contact.Depth)
                Deepest.set(Contact.Surface, Contact);
        for (const Contact of Deepest.values())
            this.Position = MultiplyAddVector(this.Position, Contact.Normal, Math.min(Contact.Depth, 0.25) * 0.8);
        // ② impulses, deepest first
        Contacts.sort((Left, Right) => Right.Depth - Left.Depth);
        for (const Contact of Contacts)
        {
            const Normal = Contact.Normal;
            const Lever = RotateVector(this.Orientation, Contact.Local);
            const Velocity = AddVector(this.LinearVelocity, CrossVector(this.AngularVelocity, Lever));
            const Approach = DotVector(Velocity, Normal);
            const Tangential = SubtractVector(Velocity, ScaleVector(Normal, Approach));
            const Sliding = LengthVector(Tangential);
            if (Sliding > ScrapeSpeed)
                this.RecordScrape(`hull${Contact.Index >> 3}`, AddVector(this.Position, MultiplyAddVector(Lever, Normal, -Radius)), Tangential,
                                  Math.min(1.5, 0.06 * Sliding * Math.min(1.0, 0.3 + Contact.Depth * 20.0)), -1, Normal);
            if (Approach >= 0.0)
                continue;
            const Angular = CrossVector(InverseInertiaWorld(CrossVector(Lever, Normal)), Lever);
            const Effective = 1.0 / VehicleGeometry.Mass + DotVector(Normal, Angular);
            const Impulse = -(1.0 + 0.12) * Approach / Effective;
            let Total = ScaleVector(Normal, Impulse);
            if (Sliding > 1.0e-4)
                Total = AddVector(Total, ScaleVector(Tangential, -Math.min(0.42 * Impulse, Sliding / Effective) / Sliding));
            this.LinearVelocity = MultiplyAddVector(this.LinearVelocity, Total, 1.0 / VehicleGeometry.Mass);
            this.AngularVelocity = AddVector(this.AngularVelocity, InverseInertiaWorld(CrossVector(Lever, Total)));
        }
    }

    // 📝 Keeps the strongest scrape per contact per frame: Key names the contact (a hull patch, a rim). Velocity is the
    //    metal's slide over the surface at the contact; a rim adds its frame { Forward, SpinSpeed, Radius }.
    RecordScrape(Key, Point, Velocity, Strength, Wheel = -1, Normal = [0.0, 0.0, 1.0], Rim = null)
    {
        const Prior = this.Scrapes.get(Key);
        if (!Prior || Prior.Strength < Strength)
            this.Scrapes.set(Key, { Point: Point.slice(), Velocity: Velocity.slice(), Strength, Wheel, Normal: Normal.slice(),
                                    Rim: Rim ? { Forward: Rim.Forward.slice(), SpinSpeed: Rim.SpinSpeed, Radius: Rim.Radius } : null });
    }

    // 📝 Sphere hull against the course surface and the static obstacle boxes: positional projection plus a
    //    restitution-and-friction impulse at the deepest contact of each sphere. The point hull replaces it once
    //    assigned.
    ResolveHullContacts()
    {
        const InverseInertiaWorld = (Vector) =>
        {
            const Body = RotateVector(ConjugateQuaternion(this.Orientation), Vector);
            return RotateVector(this.Orientation, [Body[0] / this.InertiaBody[0], Body[1] / this.InertiaBody[1], Body[2] / this.InertiaBody[2]]);
        };
        if (this.HullPoints)
        {
            this.ResolveHullPoints(InverseInertiaWorld);
            return;
        }
        for (const [X, Y, Z, Radius] of HullSpheres)
        {
            const Centre = AddVector(this.Position, RotateVector(this.Orientation, [X, Y, Z]));
            let Normal = null, Depth = 0.0;
            const Ground = ResolveCourseHeight(Centre[0], Centre[1]);
            if (Centre[2] - Radius < Ground)
            {
                Normal = [0.0, 0.0, 1.0];
                Depth = Ground + Radius - Centre[2];
            }
            for (const Box of this.NearObstacles || this.Obstacles)
            {
                const Closest = [ClampScalar(Centre[0], Box.Minimum[0], Box.Maximum[0]),
                                 ClampScalar(Centre[1], Box.Minimum[1], Box.Maximum[1]),
                                 ClampScalar(Centre[2], Box.Minimum[2], Box.Maximum[2])];
                const Offset = SubtractVector(Centre, Closest);
                const Distance = LengthVector(Offset);
                if (Distance >= Radius)
                    continue;
                let BoxNormal, BoxDepth;
                if (Distance > 1.0e-6)
                {
                    BoxNormal = ScaleVector(Offset, 1.0 / Distance);
                    BoxDepth = Radius - Distance;
                }
                else
                {
                    const Exits = [Centre[0] - Box.Minimum[0], Box.Maximum[0] - Centre[0], Centre[1] - Box.Minimum[1],
                                   Box.Maximum[1] - Centre[1], Centre[2] - Box.Minimum[2], Box.Maximum[2] - Centre[2]];
                    const Face = Exits.indexOf(Math.min(...Exits));
                    BoxNormal = [0.0, 0.0, 0.0];
                    BoxNormal[Face >> 1] = (Face & 1) ? 1.0 : -1.0;
                    BoxDepth = Exits[Face] + Radius;
                }
                if (BoxDepth > Depth)
                {
                    Normal = BoxNormal;
                    Depth = BoxDepth;
                }
            }
            if (!Normal)
                continue;
            this.Position = MultiplyAddVector(this.Position, Normal, Depth * 0.8);
            const Lever = SubtractVector(MultiplyAddVector(Centre, Normal, -Radius), this.Position);
            const Velocity = AddVector(this.LinearVelocity, CrossVector(this.AngularVelocity, Lever));
            const Approach = DotVector(Velocity, Normal);
            if (Approach >= 0.0)
                continue;
            const Angular = CrossVector(InverseInertiaWorld(CrossVector(Lever, Normal)), Lever);
            const Effective = 1.0 / VehicleGeometry.Mass + DotVector(Normal, Angular);
            const Impulse = -(1.0 + 0.15) * Approach / Effective;
            const Tangential = SubtractVector(Velocity, ScaleVector(Normal, Approach));
            const TangentialSpeed = LengthVector(Tangential);
            let Total = ScaleVector(Normal, Impulse);
            if (TangentialSpeed > 1.0e-4)
                Total = AddVector(Total, ScaleVector(Tangential, -Math.min(0.45 * Impulse, TangentialSpeed * VehicleGeometry.Mass * 0.5) / TangentialSpeed));
            this.LinearVelocity = MultiplyAddVector(this.LinearVelocity, Total, 1.0 / VehicleGeometry.Mass);
            this.AngularVelocity = AddVector(this.AngularVelocity, InverseInertiaWorld(CrossVector(Lever, Total)));
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    POSES
    //--------------------------------------------------------------------------------------------------------------------

    QueryBodyTransform()
    {
        return ConstructRigidTransform(this.Position, this.Orientation);
    }

    // 📝 Hub = strut top + body −Z × strut length; rotation = body · steer about +Z · spin about the +Y axle.
    QueryWheelTransform(Index)
    {
        const Wheel = this.Wheels[Index];
        const HubLocal = [Wheel.Mount[0], Wheel.Mount[1], Wheel.Mount[2] - Wheel.Length];
        const Hub = AddVector(this.Position, RotateVector(this.Orientation, HubLocal));
        const Steered = MultiplyQuaternion(this.Orientation, ConstructAxisQuaternion([0.0, 0.0, 1.0], Wheel.Steer));
        const Spun = MultiplyQuaternion(Steered, ConstructAxisQuaternion([0.0, 1.0, 0.0], Wheel.SpinAngle));
        return ConstructRigidTransform(Hub, Spun);
    }

    // 📝 Per-wheel contact readings averaged over the substeps since the last drain: sliding power [W], the share of
    //    the interval spent in contact, mean slip speed [m/s] while in contact, the last contact point and contact
    //    velocity [world] and the hub position. The accumulators restart from zero.
    DrainWheelReadings()
    {
        return this.Wheels.map((Wheel, Index) =>
        {
            const Hub = this.QueryWheelTransform(Index).slice(12, 15);
            const Seconds = Wheel.SampleSeconds;
            const OnDebris = Wheel.SupportSeconds > 0.0;
            Wheel.SupportSeconds = 0.0;
            const Reading = {
                OnDebris,
                Seconds,
                SlidingPower:    Seconds > 0.0 ? Wheel.SlidingEnergy / Seconds : 0.0,
                ContactFraction: Seconds > 0.0 ? Wheel.ContactSeconds / Seconds : (Wheel.Contact ? 1.0 : 0.0),
                SlipSpeed:       Wheel.ContactSeconds > 0.0 ? Wheel.SlipTravel / Wheel.ContactSeconds : 0.0,
                ContactPoint:    Wheel.Contact && Wheel.ContactPoint ? Wheel.ContactPoint.slice() : [Hub[0], Hub[1], Hub[2] - Wheel.Radius],
                Shredded:        Wheel.Shredded,
                ContactVelocity: Wheel.ContactVelocity.slice(),
                TreadVelocity:   Wheel.TreadVelocity ? Wheel.TreadVelocity.slice() : [0.0, 0.0, 0.0],
                HubPosition:     Hub,
                Airspeed:        LengthVector(this.LinearVelocity)
            };
            Wheel.SlidingEnergy = 0.0;
            Wheel.ContactSeconds = 0.0;
            Wheel.SampleSeconds = 0.0;
            Wheel.SlipTravel = 0.0;
            return Reading;
        });
    }

    AssignGrip(Factors)
    {
        Factors.forEach((Factor, Index) => { this.Wheels[Index].Grip = Factor; });
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 TYRE STATE
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Gauge pressures [Pa] from the thermal model's gas law (air mass, gas temperature, leaks), once per frame.
    AssignTyrePressures(Pressures)
    {
        this.Pressures = Pressures.slice();
        Pressures.forEach((Pressure, Index) => { this.Wheels[Index].Pressure = Pressure; });
    }

    // 📝 Once per frame: a flat rolled far enough sheds its rubber and the wheel runs on the rim (radius rim + margin);
    //    a rim on the road — pinched through a flat, or bare — throws sparks while it moves.
    AdvanceTyreWear(Seconds)
    {
        this.Wheels.forEach((Wheel, Index) =>
        {
            if (!Wheel.Shredded && Wheel.Contact && Wheel.Pressure < ShredPressure)
            {
                Wheel.ShredDistance += Wheel.RollingSpeed * Seconds;
                if (Wheel.ShredDistance > ShredDistance)
                    this.ShredTyre(Index);
            }
            const Grinding = Math.max(Wheel.RollingSpeed, LengthVector(Wheel.ContactVelocity));
            if (Wheel.Contact && Wheel.ContactPoint && (Wheel.Pinched || Wheel.Shredded) && Grinding > 1.5)
            {
                // 📝 The rim's frame at the patch: the road's normal, the wheel's heading laid onto the road, and the rim's
                //    own surface speed about the hub, S = Spin·r (positive rolling forward). The metal at the patch moves
                //    over the road at Slip = v − S·F — nearly nothing for a rim that rolls, the whole speed for one that
                //    is locked. SparkSequence detaches each spark a little behind the patch, where the metal has just left
                //    the road, at that point's own velocity: a rolling rim throws its sparks up off its trailing edge and
                //    they stream back from the car, a locked one drags them along the slide.
                const Up = Wheel.GroundNormal;
                const Steered = MultiplyQuaternion(this.Orientation, ConstructAxisQuaternion([0.0, 0.0, 1.0], Wheel.Steer));
                const Heading = RotateVector(Steered, [1.0, 0.0, 0.0]);
                const Forward = NormalizeVector(SubtractVector(Heading, ScaleVector(Up, DotVector(Heading, Up))));
                const RimRadius = VehicleGeometry.TyreRimRadius + RimRadiusMargin;
                const SpinSpeed = Wheel.Spin * RimRadius;
                const Slip = SubtractVector(Wheel.ContactVelocity, ScaleVector(Forward, SpinSpeed));
                this.RecordScrape(`rim${Index}`, Wheel.ContactPoint, Slip, Math.min(1.6, (Wheel.Shredded ? 0.05 : 0.03) * Grinding), Index, Up,
                                  { Forward, SpinSpeed, Radius: RimRadius });
            }
        });
    }

    ShredTyre(Index)
    {
        const Wheel = this.Wheels[Index];
        if (Wheel.Shredded)
            return;
        Wheel.Shredded = true;
        Wheel.Radius = VehicleGeometry.TyreRimRadius + RimRadiusMargin;
        Wheel.TyreDeflection = 0.0;
        Wheel.Pinched = false;
        this.ShredEvents.push(Index);
    }

    // 📝 Fresh rubber on every corner (the thermal model refills the air).
    RestoreTyres()
    {
        for (const Wheel of this.Wheels)
            Object.assign(Wheel, { Radius: VehicleGeometry.TyreRadius, TyreDeflection: 0.016, Pinched: false, Shredded: false, ShredDistance: 0.0 });
    }

    // 📝 Hands the frame's shred, pinch and scrape events to the host.
    ResolveEvents()
    {
        const Events = { Shreds: this.ShredEvents, Pinches: this.PinchEvents, Sparks: [...this.Scrapes.values()] };
        this.ShredEvents = [];
        this.PinchEvents = [];
        this.Scrapes = new Map();
        return Events;
    }

    // 📝 What the carcass solver reads per wheel: the hub's height over the road, the road's direction in the wheel's
    //    own frame (steer and spin included), the pressure and the cornering force.
    QuerySteeringFeedback()
    {
        const Local = RotateVector(ConjugateQuaternion(this.Orientation), this.LinearVelocity);
        const Angular = RotateVector(ConjugateQuaternion(this.Orientation), this.AngularVelocity);
        return { Speed: Local[0], LateralSpeed: Local[1], YawRate: Angular[2],
            Contacts: this.Wheels.filter((Wheel) => Wheel.Contact).length };
    }

    QueryForward()
    {
        return RotateVector(this.Orientation, [1.0, 0.0, 0.0]);
    }

    QueryTelemetry()
    {
        const Forward = this.QueryForward();
        const Gear = this.GearIndex === TransmissionSheet.ReverseIndex ? "R" : String(this.GearIndex - TransmissionSheet.FirstIndex + 1);
        return {
            Speed: DotVector(this.LinearVelocity, Forward),
            Gear,
            EngineRPM: Math.min(this.EngineRPM, EngineSheet.RedlineRPM + 400.0),
            Boost: this.Boost,
            Throttle: this.Throttle,
            Contacts: this.Wheels.filter((Wheel) => Wheel.Contact).length,
            Skid: Math.max(...this.Wheels.map((Wheel) => Wheel.Skid)),
            Burnout: this.Burnout,
            RearSpin: 0.5 * (this.Wheels[2].Spin * this.Wheels[2].Radius + this.Wheels[3].Spin * this.Wheels[3].Radius) - 0.012 * 0.5 * (this.Wheels[2].Spin + this.Wheels[3].Spin),
            Steer: this.SteerAngle
        };
    }
}
