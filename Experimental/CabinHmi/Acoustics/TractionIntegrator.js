// TRACTION INTEGRATOR
// 🧩 The longitudinal car model that drives the sounds: engine speed from torque, inertia and friction; turbo boost with
//    lag; a dual-clutch gearbox with ignition-cut upshifts and throttle-blip downshifts; launch clutch slip; the rev
//    limiter; traction-limited wheelspin; brakes with ABS, drag and rolling resistance; and a lateral grip check for
//    cornering squeal. It also reports acceleration for the suspension and brake voices.
// 📝 Step(Seconds) advances everything and returns the feed for PulseTrainEngine plus speed, gear and slip for the tyre
//    and road sounds. Gear 0 is neutral. The start sequence (starter, catch, flare, settle) also lives here.

import { Clamp, Mix, Smoothstep } from './SignalKit.js';
import { LaFerrari, FullTorque, BoostTarget } from './PulseTrainEngine.js';

const Gravity = 9.81, AirDensity = 1.225, RpmPerRadian = 60 / (2 * Math.PI);

export class TractionIntegrator
{
    constructor(Profile = LaFerrari)
    {
        this.Profile = Profile;
        this.Running = false;
        this.Starting = null;                                          // seconds since the start button, during the start
        this.Rpm = 0;
        this.Speed = 0;                                                // m/s
        this.Gear = 0;
        this.Throttle = 0; this.Brake = 0; this.Steer = 0; this.Handbrake = false;
        this.Automatic = true;
        this.Traction = true;                                          // traction control: trims torque at the grip limit
        this.Abs = true;                                               // anti-lock brakes: pulse the pressure instead of locking
        this.AbsActive = false;
        this.Boost = 0; this.Spool = 0; this.LagScale = 1; this.TransBrake = false; this.Clunk = 0; this.LastGear = 0;
        this.Accel = 0; this.Lateral = 0;
        this.Shift = null;                                             // { Way: 'Up' | 'Down', Age, To }
        this.Limiting = false;
        this.Wheelspin = 0; this.Corner = 0; this.LockUp = 0;
        this.Distance = 0;
        this.Idle = Profile.Idle;
        this.Feed = { Rpm: 0, Throttle: 0, Load: 0, Ignition: 1, Motor: 0, Starter: 0, Boost: 0, Gear: 0, Clunk: 0 };
    }

    Start() { if (!this.Running && !this.Starting) this.Starting = 0; }
    Stop() { this.Running = false; this.Starting = null; this.Gear = 0; }

    /** Engine speed the wheels would turn the engine at in a gear (clutch closed). */
    WheelRpm(Gear = this.Gear)
    {
        if (Gear <= 0) return 0;
        const P = this.Profile;
        return this.Speed / P.TyreRadius * P.Ratios[Gear - 1] * P.Final * RpmPerRadian;
    }

    ShiftUp()
    {
        if (this.Shift || !this.Running) return;
        if (this.Gear === 0) { this.Gear = 1; return; }
        if (this.Gear >= this.Profile.Ratios.length) return;
        this.Shift = { Way: 'Up', Age: 0, To: this.Gear + 1 };
    }

    ShiftDown()
    {
        if (this.Shift || !this.Running || this.Gear <= 1) { if (this.Gear === 1 && this.Speed < 1) this.Gear = 0; return; }
        if (this.WheelRpm(this.Gear - 1) > this.Profile.Limiter - 150) return;     // would over-rev: refuse
        this.Shift = { Way: 'Down', Age: 0, To: this.Gear - 1 };
    }

    Step(Seconds)
    {
        const P = this.Profile;
        const Feed = this.Feed;
        Feed.Starter = 0;
        let Throttle = this.Throttle, Ignition = 1, Motor = 0;

        // Start sequence: crank 0.75 s, catch and flare to ~2900 rpm, settle on the cold idle, then warm idle.
        if (this.Starting !== null)
        {
            const Age = (this.Starting += Seconds);
            if (Age < 0.75) { this.Rpm = Mix(this.Rpm, 230 + 30 * Math.sin(Age * 40), 0.2); Feed.Starter = 1; Throttle = 0; }
            else if (Age < 1.05) { Throttle = 0.7; Feed.Starter = Age < 0.85 ? 0.6 : 0; }
            else if (Age < 1.4) Throttle = 0.08;
            else { this.Starting = null; this.Running = true; this.Idle = P.ColdIdle; }
            if (Age >= 0.75 && this.Rpm < 600) this.Rpm = 600;
        }
        if (this.Running) this.Idle = Mix(this.Idle, P.Idle, Seconds / 6);

        // Shift choreography (DCT): upshift = ignition cut then clutch swap; downshift = blip then swap (times per car).
        let Clutch = 1;
        const Cut = P.Gearbox?.Cut ?? 0.045, Swap = Cut + (P.Gearbox?.Swap ?? 0.07);
        if (this.Shift)
        {
            const Shift = this.Shift;
            Shift.Age += Seconds;
            if (Shift.Way === 'Up')
            {
                if (Shift.Age < Cut) { Ignition = 0; Clutch = 0.6; }
                else if (Shift.Age < Swap) { this.Gear = Shift.To; Ignition = Smoothstep(Cut, Swap, Shift.Age); Clutch = 0.5; }
                else this.Shift = null;
            }
            else
            {
                if (Shift.Age < 0.11) { Throttle = Math.max(Throttle, 0.75); Clutch = 0; }
                else if (Shift.Age < 0.2) { this.Gear = Shift.To; Clutch = 0.4; }
                else this.Shift = null;
            }
        }

        // Automatic mode: upshift near the limiter under power, downshift when slow or braking.
        if (this.Automatic && this.Running && !this.Shift && this.Gear > 0)
        {
            if (this.Rpm > P.Redline && this.Throttle > 0.3 && this.Gear < P.Ratios.length) this.ShiftUp();
            else if (this.Gear > 1 && ((this.Rpm < 2600 && this.Throttle > 0.4) || (this.Brake > 0.15 && this.Rpm < 3600) || this.Rpm < 1500)) this.ShiftDown();
        }

        // Rev limiter with hysteresis (hard fuel/ignition cut, bounces at ~15–20 Hz).
        if (this.Rpm > P.Limiter) this.Limiting = true;
        if (this.Rpm < P.Limiter - 140) this.Limiting = false;
        if (this.Limiting) Ignition = 0;

        if (!this.Running && this.Starting === null) { Throttle = 0; Ignition = 0; }
        const Alive = this.Running || (this.Starting !== null && this.Starting >= 0.75);

        // Idle control: open the throttle a touch when the engine sags below idle.
        let Effective = Throttle;
        if (Alive && this.Rpm < this.Idle + 150) Effective = Math.max(Effective, Clamp((this.Idle + 150 - this.Rpm) / 900, 0, 0.35) + 0.035);

        // Torques (Nm). Engine torque blends between pumping drag (closed) and the full-load curve.
        const Full = FullTorque(P, Math.max(this.Rpm, 600));
        const Friction = this.Rpm > 50 ? 38 + this.Rpm * 0.021 : 0;
        if (P.Turbo) this.Turbo(Seconds, Effective, Alive);
        else if (P.Stock || P.Induction) this.Boost = Alive ? Math.pow(Clamp(Effective, 0, 1), 1.5) * Smoothstep(600, 2200, this.Rpm) : 0;   // belt-driven blower: no lag
        const Charge = P.Turbo ? Mix(P.Turbo.Unboosted, 1, this.Boost) : 1;
        const Combustion = Alive ? (Full * Charge + Friction) * Math.pow(Effective, 0.75) * Ignition : 0;   // the curve is net of friction
        if (Alive && this.Gear > 0 && this.Running && P.Motor)
            Motor = this.Brake > 0.05 ? -Math.min(1, this.Brake * 1.5) * Smoothstep(3, 12, this.Speed) : Smoothstep(0.55, 1, Throttle) * (1 - Smoothstep(6500, 9000, this.Rpm) * 0.6);
        const Electric = P.Motor ? Math.max(0, Motor) * P.Motor.Torque : 0;
        const EngineNet = Combustion - Friction;

        // Drivetrain: neutral / slipping clutch revs freely; locked clutch ties engine speed to the wheels.
        const Inertia = 0.32, Ratio = this.Gear > 0 ? P.Ratios[this.Gear - 1] * P.Final : 0;
        let DriveForce = 0;
        const Coupled = this.Gear > 0 && this.Running && !(this.Shift && this.Shift.Way === 'Down' && this.Shift.Age < 0.11);
        const Wheel = this.WheelRpm();
        if (!Coupled || Clutch === 0)
        {
            this.Rpm += EngineNet / Inertia * RpmPerRadian * Seconds;
            if (this.Gear > 0 && Coupled === false && this.Shift?.Way === 'Down' && this.Rpm > this.WheelRpm(this.Shift.To) + 300) this.Rpm -= 4000 * Seconds;
        }
        else
        {
            // Launch: the clutch slips until the wheels catch up with a launch speed that rises with throttle.
            const Launch = Mix(this.Idle + 250, P.Drive?.Launch ?? 4300, Throttle);
            if (Wheel < Launch - 200 && this.Gear === 1)
            {
                const Target = Math.max(Wheel, Launch);
                this.Rpm += (Target - this.Rpm) * Math.min(1, Seconds * 7);
                DriveForce = (Combustion + Electric) * 0.95 * Ratio * 0.92 / P.TyreRadius;
            }
            else
            {
                const Spinning = this.Wheelspin > 0 ? 1 + this.Wheelspin * 0.25 : 1;
                const Target = Math.max(Wheel * Spinning, Alive ? this.Idle * 0.92 : 0);
                const Swap = this.Shift ? 18 : 60;                      // softer pull-in while the clutches swap
                this.Rpm += (Target - this.Rpm) * Math.min(1, Seconds * Swap);
                DriveForce = (EngineNet + Friction * 0.25 + Electric) * Clutch * Ratio * 0.92 / P.TyreRadius;
            }
        }
        if (!Alive && this.Starting === null) this.Rpm = Math.max(0, this.Rpm - (this.Rpm * 2.5 + 200) * Seconds);
        this.Rpm = Clamp(this.Rpm, 0, P.Limiter + 400);

        // Front-axle motor (918 Spyder): torque through its own gear, power-limited, decoupled at its cut-off speed.
        let FrontRpm = 0;
        const Front = P.Motor?.Front;
        if (Front && this.Gear > 0)
        {
            const Engaged = 1 - Smoothstep(Front.Cutoff - 3, Front.Cutoff, this.Speed);
            FrontRpm = Engaged > 0 ? this.Speed / P.TyreRadius * 60 / (2 * Math.PI) * Front.Ratio : 0;
            if (Motor > 0) DriveForce += Motor * Engaged * Math.min(Front.Torque * Front.Ratio * 0.92 / P.TyreRadius, Front.Power / Math.max(this.Speed, 1));
        }
        this.Road(DriveForce, Seconds);
        // TransBrake (drag automatics): first and reverse engaged at once lock the output; the engine is held at the
        // launch rpm against the converter and the car leaps forward the moment it is released.
        if (this.TransBrake && this.Gear === 1) { this.Speed = 0; this.Accel = 0; this.Wheelspin = 0; }

        if (this.Gear !== this.LastGear) { if (this.LastGear !== 0 || this.Speed > 0.5) this.Clunk++; this.LastGear = this.Gear; }

        const Load = Alive ? Clamp((Combustion - Friction * 0.2) / Math.max(Full, 1), 0, 1) : 0;
        Object.assign(Feed, { Rpm: this.Rpm, Throttle: Alive ? Effective : 0, Load, Ignition: Alive ? Ignition : 0, Motor, Boost: this.Boost, Spool: P.Turbo ? this.Spool : undefined, Gear: this.Gear, Clunk: this.Clunk, FrontRpm });
        return Feed;
    }


    /**
     * Turbo lag, from the shaft rather than a fixed delay. The turbine wheel has inertia and is driven by exhaust
     * energy (engine speed × load, and more boost makes more exhaust: positive feedback), so from low rpm it spools
     * slowly, then comes on with a rush as boost feeds itself. The compressor's pressure goes with shaft speed squared
     * and the wastegate caps it at full boost. Lifting closes the throttle and the blow-off valve dumps the pressure at
     * once, but the shaft coasts down over about a second, so a quick re-apply finds it still spinning and boost comes
     * straight back. LagScale stretches (2 = twice the lag) or removes (0) the lag; Turbo.Lag sets the shaft's inertia.
     */
    Turbo(Seconds, Effective, Alive)
    {
        const T = this.Profile.Turbo, Inertia = T.Lag * this.LagScale;
        const Steady = Alive ? BoostTarget(this.Profile, this.Rpm, Effective) : 0;                 // the steady-state map
        const Exhaust = Alive ? Math.pow(Clamp(this.Rpm / T.Full, 0, 1.6), 1.7) * (0.15 + 0.85 * Effective) * (0.5 + 0.5 * this.Boost) : 0;
        const Floor = Alive ? 0.12 * Smoothstep(600, 6000, this.Rpm) * (0.3 + 0.7 * Effective) : 0;   // exhaust always turns it a little
        const Goal = Math.max(Math.sqrt(Steady), Floor);
        const Rate = Goal > this.Spool ? (0.15 + 2.6 * Exhaust) / Math.max(Inertia, 0.004) : 0.8 / (Math.max(Inertia, 0.004) + 0.35);
        this.Spool += (Goal - this.Spool) * Math.min(1, Seconds * Rate);
        if (T.Assist && Alive && Effective > 0.1) this.Spool += (Math.max(this.Spool, 0.35 + 0.35 * Effective) - this.Spool) * Math.min(1, Seconds / 0.08);   // e-motor pre-spins it
        const Pressure = Math.min(this.Spool * this.Spool, Math.pow(Clamp(Effective, 0, 1), 1.5) * Smoothstep(T.Onset * 0.6, T.Full, this.Rpm) * 1.02);
        this.Boost += (Pressure - this.Boost) * Math.min(1, Seconds / (Pressure > this.Boost ? 0.05 : 0.08));
    }

    /** Tyres and road: traction limit on the driven axles, brakes with ABS, drag, rolling resistance, cornering slip. */
    Road(DriveForce, Seconds)
    {
        // Tyres: traction limit on the driven rear axle, brakes with ABS, drag and rolling resistance.
        const P = this.Profile, Weight = P.Mass * Gravity;
        const Grip = (P.Drive?.Grip ?? 1.5) * Weight * (P.Drive?.Share ?? 0.6) * (1 + this.Speed * this.Speed * 0.00012);
        const Excess = DriveForce > Grip ? (DriveForce - Grip) / Grip : 0;
        const Spin = this.Traction ? Math.min(Excess * 1.5, 0.12) : Clamp(Excess * 2.5 + (this.Handbrake ? 0 : 0), 0, 1);
        this.Wheelspin = Spin > this.Wheelspin ? Spin : Math.max(Spin, this.Wheelspin - Seconds * 3);
        DriveForce = Math.min(DriveForce, Grip * (this.Traction ? 1 : 0.85 + this.Wheelspin * 0.05));
        const Locking = Smoothstep(0.85, 1, this.Brake) * Smoothstep(2, 8, this.Speed);
        this.AbsActive = this.Abs && Locking > 0.05;
        const BrakeForce = this.Brake * (this.AbsActive ? 1.28 : 1.35) * Weight + (this.Handbrake ? 0.45 * Weight : 0);
        this.LockUp = (this.AbsActive ? 0.08 : 0.6) * Locking + (this.Handbrake ? Smoothstep(1, 5, this.Speed) : 0);
        const Resist = 0.5 * AirDensity * P.DragArea * this.Speed * this.Speed + 0.012 * Weight;
        const Net = DriveForce - (this.Speed > 0.05 ? BrakeForce + Resist : 0) * Math.sign(this.Speed || 1);
        // Drive.Rotating (opt-in): wheels, driveline and flywheel add effective mass, most in the low gears (the usual
        // mass factor 1.04 + 0.0025 × overall ratio²; 1.37 in a Mustang's 1st, 1.07 in 4th).
        const Overall = P.Drive?.Rotating && this.Gear > 0 ? P.Ratios[this.Gear - 1] * P.Final : 0;   // EVs share Road() and have no Ratios
        const Spun = P.Drive?.Rotating ? 1.04 + 0.0025 * Overall * Overall : 1;
        const Before = this.Speed;
        this.Speed = Math.max(0, this.Speed + Net / (P.Mass * Spun) * Seconds);
        this.Accel += ((this.Speed - Before) / Math.max(Seconds, 1e-4) - this.Accel) * Math.min(1, Seconds * 20);
        if (DriveForce <= 0 && this.Speed < 0.05 && this.Brake > 0) this.Speed = 0;
        this.Distance += this.Speed * Seconds;

        // Cornering: lateral demand against grip → slip share for the squeal.
        const Lateral = Math.abs(this.Steer) * this.Speed * this.Speed / 18;
        this.Lateral = Lateral;
        this.Corner = Smoothstep(0.75, 1.25, Lateral / (1.25 * Gravity));
    }

    /** Telemetry for the bench readouts. */
    Readout()
    {
        return {
            Rpm: this.Rpm, Kmh: this.Speed * 3.6, Gear: this.Gear, Running: this.Running, Starting: this.Starting !== null,
            Throttle: this.Throttle, Brake: this.Brake, Motor: this.Feed.Motor, Wheelspin: this.Wheelspin, Corner: this.Corner,
            LockUp: this.LockUp, Limiting: this.Limiting, Shifting: !!this.Shift, Boost: this.Boost, Spool: this.Spool, Abs: this.AbsActive,
            Accel: this.Accel, Lateral: this.Lateral, Car: this.Profile.Name,
        };
    }
}
