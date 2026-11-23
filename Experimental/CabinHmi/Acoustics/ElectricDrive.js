// ELECTRIC DRIVE
// 🧩 Electric cars for the sound bench: the drive unit voice (permanent-magnet motors, the inverter's switching tones,
//    reduction gear whine, rotor windage, an optional synthetic "sport sound" layer, shift thunks and a ready chime) and
//    the electric car model that feeds it (motor torque and power limits, a multi-speed gearbox, regeneration).
// 📝 Two cars: a Porsche Taycan Turbo S (two PSM motors, single-speed front, two-speed rear 15.5 / 8.05, 16 000 rpm)
//    and an original Next-Gen racer voiced in the spirit of Jackson Storm from Cars 3 (an electric whir that shifts
//    through a sequential gearbox, with a V8 undertone). Nothing is sampled from Porsche or from the film.
//    The motor tones are not drawn as a sweep: each one is an engine order of the motor shaft (pole pairs × 6 and 12
//    for torque ripple, the slot order, gear tooth counts), so every pitch follows the real shaft speed.

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, OnePole, Biquad, ModeBank, DcBlock, DelayLine, Slew, PanGains, SoftClip, NarrowTone, PolySaw, Wander } from './SignalKit.js';
import { TractionIntegrator } from './TractionIntegrator.js';

const RpmPerRadian = 60 / (2 * Math.PI);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARS
//------------------------------------------------------------------------------------------------------------------------

/**
 * Porsche Taycan Turbo S (J1, 2020): permanent-magnet synchronous motor on each axle, 800 V. Front: 440 Nm, single-speed
 * 8.05:1. Rear: 610 Nm, two-speed 15.5:1 / 8.05:1, shifts to 2nd somewhere up to ~100 km/h. 560 kW with launch
 * control overboost, 1050 Nm, 16 000 rpm motors, 2295 kg, 260 km/h, 0–100 km/h 2.8 s, 0–200 km/h 9.8 s. System is the
 * power that reaches the road in this model (460 kW, with grip share 0.78), tuned so the launch lands on those times.
 * Pole pairs, slot count, gear tooth counts and the inverter carrier are informed guesses; the ratios, torques and
 * motor powers are published figures.
 */
export const TaycanTurboS = {
    Name: 'Porsche Taycan Turbo S', Code: '2 × PSM · 800 V · 2-speed rear', Electric: true,
    Motors: [
        { Axle: 'Front', Torque: 440, Power: 190000, Ratios: [8.05], PolePairs: 4, Slots: 48, Carrier: 9200, Level: 0.75, Pan: -0.12,
          Orders: [[8, 0.25], [24, 1], [48, 0.55], [72, 0.2]], Meshes: [[[29, 0.9], [8.3, 0.45]]] },
        { Axle: 'Rear', Torque: 610, Power: 400000, Ratios: [15.5, 8.05], PolePairs: 4, Slots: 48, Carrier: 8000, Level: 1, Pan: 0.12,
          Orders: [[8, 0.25], [24, 1], [48, 0.6], [72, 0.22]], Meshes: [[[21, 1], [7.4, 0.6], [3.1, 0.3]], [[21, 1], [11.6, 0.5]]] },
    ],
    System: 460000, MaxRpm: 16000, Redline: 15500, Limiter: 16000, Idle: 0, TopKmh: 260,
    TyreRadius: 0.358, Mass: 2295 + 75, DragArea: 0.51, Drive: { Share: 0.78 },
    Shift: { Cut: 0.16, Blend: 0.3, Dip: 0.35, UpKmh: [28, 100], KickKmh: 60 },
    Regen: 265000, Clunk: { Modes: [[92, 0.08], [260, 0.05, 0.6], [710, 0.03, 0.3]], Gain: 0.18 },
    Voice: { Whine: 0.12, Inverter: 0.022, Gears: 0.06, Windage: 0.04, Sport: 0, Whir: 0, Undertone: 0 },
    Sport: { Low: 42, High: 150, Bright: 2600 }, Chime: [880, 1318.5], Space: 0.2,
};

/**
 * Next-Gen racer: an original voice in the spirit of Jackson Storm (Cars 3), who is listed as an 850 hp, 214 mph
 * custom racer and in the film sounds like an electric car with a gearbox. Here: one 634 kW rear motor to 18 000 rpm
 * through a six-speed sequential with straight-cut gears, a bright twin-tone whir that drops in pitch on every
 * upshift, a hard pneumatic shift clack, and a low V8 undertone. Figures are chosen for the character, not measured.
 */
export const NextGenRacer = {
    Name: 'Next-Gen racer', Code: 'Storm-style · 634 kW motor · 6-speed sequential', Electric: true,
    Motors: [
        { Axle: 'Rear', Torque: 900, Power: 634000, Ratios: [20, 15, 12, 9.8, 8.1, 6.7], PolePairs: 5, Slots: 60, Carrier: 12000, Level: 1, Pan: 0,
          Orders: [[10, 0.3], [30, 1], [60, 0.5]], Meshes: [16, 18, 20, 22, 24, 26].map((Teeth) => [[Teeth, 1], [Teeth * 0.37, 0.4]]) },
    ],
    System: 634000, MaxRpm: 18000, Redline: 17200, Limiter: 18000, Idle: 0, TopKmh: 344,
    TyreRadius: 0.34, Mass: 1450 + 75, DragArea: 1.1, Drive: { Share: 0.55 },
    Shift: { Cut: 0.055, Blend: 0.06, Dip: 0, UpRpm: 17000, DownRpm: 11500 },
    Regen: 300000, Clunk: { Modes: [[185, 0.035], [640, 0.022, 0.8], [1900, 0.012, 0.5], [4300, 0.006, 0.3]], Gain: 0.4 },
    Voice: { Whine: 0.06, Inverter: 0.012, Gears: 0.08, Windage: 0.05, Sport: 0, Whir: 0.13, Undertone: 0.55 },
    Whir: { Order: 6, Fifth: 1.5, Vibrato: 6.5 }, Undertone: { Ratio: 0.5 }, Chime: [660, 990], Space: 0.24,
};

export const ElectricCars = { TaycanTurboS, NextGenRacer };

//------------------------------------------------------------------------------------------------------------------------
//                                                    CAR MODEL
//------------------------------------------------------------------------------------------------------------------------

/**
 * Electric car model, a TractionIntegrator with motors instead of an engine: no idle, no clutch; each motor delivers
 * min(torque, power / speed) under a shared system power cap, through its own ratio. Multi-speed gearboxes shift on
 * speed (Taycan: earlier when cruising, ~100 km/h flat out, kick-down below 60) or on motor speed (the racer). The
 * front motor keeps pulling while the rear gearbox shifts. Braking regenerates (heard as negative motor load).
 * Step() returns { Rpm, Throttle, Load, Motors: [{ Rpm, Load }], Gear, Clunk, Speed, Ready } for DriveUnit.
 */
export class ElectricIntegrator extends TractionIntegrator
{
    constructor(Profile = TaycanTurboS)
    {
        super(Profile);
        this.Idle = 0;
        this.Box = Profile.Motors.reduce((Best, Motor) => (Motor.Ratios.length > Best.Ratios.length ? Motor : Best), Profile.Motors[0]);
        this.Torques = Profile.Motors.map(() => 0);
        this.Feed = { Rpm: 0, Throttle: 0, Load: 0, Motors: Profile.Motors.map(() => ({ Rpm: 0, Load: 0 })), Gear: 0, Clunk: 0, Speed: 0, Ready: 0, Boost: 0, Motor: 0 };
    }

    Start() { if (!this.Running && this.Starting === null) this.Starting = 0; }
    Stop() { this.Running = false; this.Starting = null; this.Gear = 0; }

    /** Speed of a motor at the current road speed in a gear (gear 0 still turns a motor: the wheels drive it). */
    MotorRpm(Motor, Gear = this.Gear)
    {
        const Ratio = Motor.Ratios[Clamp((Motor === this.Box ? Gear : 1) || 1, 1, Motor.Ratios.length) - 1];
        return this.Speed / this.Profile.TyreRadius * Ratio * RpmPerRadian;
    }

    WheelRpm(Gear = this.Gear) { return this.MotorRpm(this.Box, Gear); }

    ShiftUp()
    {
        if (this.Shift || !this.Running) return;
        if (this.Gear === 0) { this.Gear = 1; return; }
        if (this.Gear >= this.Box.Ratios.length) return;
        this.Shift = { Way: 'Up', Age: 0, To: this.Gear + 1 };
    }

    ShiftDown()
    {
        if (this.Shift || !this.Running || this.Gear <= 1) { if (this.Gear === 1 && this.Speed < 1) this.Gear = 0; return; }
        if (this.WheelRpm(this.Gear - 1) > this.Profile.MaxRpm - 300) return;
        this.Shift = { Way: 'Down', Age: 0, To: this.Gear - 1 };
    }

    Step(Seconds)
    {
        const P = this.Profile, Feed = this.Feed, Kmh = this.Speed * 3.6;
        Feed.Ready = 0;
        if (this.Starting !== null && (this.Starting += Seconds) >= 0.6) { this.Starting = null; this.Running = true; Feed.Ready = 1; }

        // Shift: the boxed motor's torque dips, the ratio swaps at the end of the cut, torque blends back in.
        let Scale = 1;
        if (this.Shift)
        {
            const Shift = this.Shift, { Cut, Blend, Dip } = P.Shift;
            Shift.Age += Seconds;
            if (Shift.Age < Cut) Scale = Mix(1, Dip, Smoothstep(0, Cut * 0.6, Shift.Age));
            else { this.Gear = Shift.To; Scale = Mix(Dip, 1, Smoothstep(Cut, Cut + Blend, Shift.Age)); if (Shift.Age >= Cut + Blend) this.Shift = null; }
        }

        // Automatic: speed-scheduled (two-speed) or motor-speed-scheduled (sequential).
        const Gears = this.Box.Ratios.length;
        if (this.Automatic && this.Running && !this.Shift && this.Gear > 0 && Gears > 1)
        {
            if (P.Shift.UpKmh)
            {
                if (this.Gear === 1 && Kmh > Mix(P.Shift.UpKmh[0], P.Shift.UpKmh[1], Smoothstep(0.2, 0.95, this.Throttle))) this.ShiftUp();
                else if (this.Gear === 2 && (Kmh < 12 || (this.Throttle > 0.92 && Kmh < P.Shift.KickKmh))) this.ShiftDown();
            }
            else
            {
                const Rpm = this.WheelRpm();
                if (Rpm > P.Shift.UpRpm && this.Throttle > 0.3 && this.Gear < Gears) this.ShiftUp();
                else if (this.Gear > 1 && ((Rpm < P.Shift.DownRpm && this.Brake > 0.15) || (Rpm < P.MaxRpm * 0.4 && this.Throttle > 0.9) || Rpm < P.MaxRpm * 0.22)) this.ShiftDown();   // blipped downshifts on the brakes, kick-down when bogged
            }
        }

        // Motor torques: torque limit, then power limit, then the shared system cap; regeneration under braking.
        const Demand = this.Running && this.Gear > 0 ? Clamp(this.Throttle, 0, 1) : 0;
        const Speeds = P.Motors.map((Motor) => Math.max(this.MotorRpm(Motor), 300) / RpmPerRadian);
        const Governor = 1 - Smoothstep(P.TopKmh - 4, P.TopKmh, Kmh);                         // electronically limited top speed
        const Available = P.Motors.map((Motor, Index) => Math.min(Motor.Torque, Motor.Power / Speeds[Index]) * Governor * (1 - Smoothstep(P.MaxRpm - 400, P.MaxRpm, Speeds[Index] * RpmPerRadian)));
        const Total = Available.reduce((Sum, Torque, Index) => Sum + Torque * Speeds[Index], 0);
        const Cap = Total > P.System ? P.System / Total : 1;
        const Braking = this.Running && this.Brake > 0.05 ? Math.min(1, this.Brake * 1.6) * Smoothstep(1.5, 6, this.Speed) : 0;
        let DriveForce = 0;
        P.Motors.forEach((Motor, Index) =>
        {
            const Ratio = Motor.Ratios[Clamp((Motor === this.Box ? this.Gear : 1) || 1, 1, Motor.Ratios.length) - 1];
            let Torque = Demand * Available[Index] * Cap * (Motor === this.Box ? Scale : 1);
            if (Braking > 0) Torque = -Braking * Math.min(Motor.Torque * 0.6, P.Regen * Motor.Torque / P.Motors.reduce((Sum, Each) => Sum + Each.Torque, 0) / Speeds[Index]);
            else if (Demand < 0.02 && this.Running && this.Speed > 3) Torque = -0.06 * Motor.Torque;       // light coasting regeneration
            this.Torques[Index] = Torque;
            if (Torque > 0) DriveForce += Torque * Ratio * 0.95 / P.TyreRadius;
        });
        this.Road(DriveForce, Seconds);

        // Motor speeds follow the wheels (plus wheelspin); the tachometer shows the boxed motor.
        const Spin = 1 + this.Wheelspin * 0.25;
        P.Motors.forEach((Motor, Index) => { Feed.Motors[Index].Rpm = this.MotorRpm(Motor) * Spin; Feed.Motors[Index].Load = Clamp(this.Torques[Index] / Motor.Torque, -1, 1); });
        this.Rpm = this.MotorRpm(this.Box) * Spin;
        this.Limiting = this.Rpm > P.MaxRpm - 150 && Demand > 0.3;
        if (this.Gear !== this.LastGear) { if (this.LastGear !== 0 && this.Running) this.Clunk++; this.LastGear = this.Gear; }
        const Load = Feed.Motors.reduce((Sum, Motor) => Sum + Motor.Load, 0) / Feed.Motors.length;
        Object.assign(Feed, { Rpm: this.Rpm, Throttle: Demand, Load, Gear: this.Gear, Clunk: this.Clunk, Speed: this.Speed, Motor: Load });
        return Feed;
    }

    Readout() { return { ...super.Readout(), Motor: this.Feed.Load, Boost: 0, Motors: this.Feed.Motors.map((Motor) => Math.round(Motor.Rpm)) }; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VOICE
//------------------------------------------------------------------------------------------------------------------------

/** A sine oscillator with its own phase (orders are not integers, so each tone keeps its own phase). */
class Tone
{
    constructor(Seed) { this.Phase = Seed % 1; }
    Run(Hz, Rate) { this.Phase += Hz / Rate; this.Phase -= Math.floor(this.Phase); return Math.sin(Tau * this.Phase); }
}

/**
 * Live drive unit voice. Feed it the ElectricIntegrator output, then Render() adds stereo samples into the buffers.
 * Per motor: electromagnetic orders (louder with torque, quieter in regeneration), inverter carrier with sidebands at
 * carrier ± 2 and ± 4 × electrical frequency (strongest when pulling away), reduction gear mesh whine per gear, and
 * rotor windage. Car-wide: optional sport-sound layer, the racer's whir and V8 undertone, shift thunks, ready chime.
 * Pickup: 'Chase', 'Cabin' or 'Dry' like PulseTrainEngine.
 */
export class DriveUnit
{
    constructor(Rate, Profile = TaycanTurboS, { Seed = 23, Pickup = 'Chase', Sport } = {})
    {
        this.Rate = Rate; this.Profile = Profile; this.Pickup = Pickup;
        this.Noise = new SeededNoise(Seed);
        this.Sport = Sport ?? Profile.Voice.Sport;
        this.Units = Profile.Motors.map((Motor, Index) => ({
            Motor, Rpm: new Slew(Rate, 0.01, 0), Load: new Slew(Rate, 0.04, 0),
            // Follow-up 20: every tone is a narrow noise band with drift (NarrowTone), not a locked sine.
            Orders: Motor.Orders.map((_, Order) => new NarrowTone(Rate, { Breath: 0.16 + Order * 0.05, Width: 0.004, Jitter: 0.0015, Flutter: 0.1, Seed: Seed + Index * 31 + Order })),
            Sides: [0, 1, 2, 3, 4].map((Side) => new NarrowTone(Rate, { Breath: 0.35, Width: 0.006, Jitter: 0.002, Flutter: 0.25, Seed: Seed + Index * 31 + 10 + Side })),
            Meshes: Motor.Meshes.map((Gear, Ratio) => Gear.map((_, Mesh) => new NarrowTone(Rate, { Breath: 0.3 + Mesh * 0.1, Width: 0.005, Jitter: 0.0015, Flutter: 0.15, Seed: Seed + Index * 31 + 20 + Ratio * 4 + Mesh }))),
            Spread: new Biquad(Rate), Shaft: 0,
            Drift: Motor.Orders.map(() => 1), Windage: [new Biquad(Rate), new Biquad(Rate)], Tune: Index * 17,
        }));
        this.Throttle = new Slew(Rate, 0.05, 0); this.Speed = new Slew(Rate, 0.05, 0);
        this.Gear = 0; this.Clunk = 0;
        this.ClunkRing = new ModeBank(Rate, Profile.Clunk.Modes); this.ClunkHit = 0;
        this.SportTones = [0, 1, 2, 3].map((Index) => new Tone(Index * 0.21)); this.SportTone = new Biquad(Rate).Shape('Low', 600, 0.9);
        this.WhirSaws = [new PolySaw(0), new PolySaw(0.37), new PolySaw(0.71)]; this.WhirBand = new Biquad(Rate).Shape('Band', 1800, 0.8); this.WhirPhase = 0;
        this.WhirFormants = [new Biquad(Rate), new Biquad(Rate)]; this.WhirBreath = new NarrowTone(Rate, { Breath: 0.45, Width: 0.01, Jitter: 0.003, Flutter: 0.2, Seed: Seed + 90 });
        this.WhirDrift = new Wander(Rate, 2.5, Seed + 91); this.Growl = 0;
        this.TurbineBand = new Biquad(Rate).Shape('Band', 3000, 2);
        this.Undertone = { Crank: 0, Slot: -1, Amp: 0, Level: 0, Body: new Biquad(Rate).Shape('Low', 700, 0.8), Chest: new Biquad(Rate).Shape('Peak', 115, 1.2, 6) };
        this.Chime = 0; this.ChimeTones = [new Tone(0), new Tone(0.5)];
        this.Blocks = [new DcBlock(Rate, 20), new DcBlock(Rate, 20)];
        this.Cabin = [new Biquad(Rate).Shape('Low', 5200, 0.7), new Biquad(Rate).Shape('Low', 5200, 0.7)];
        this.Air = [new OnePole(Rate, 15500), new OnePole(Rate, 15500)];
        this.Echo = new DelayLine(Math.ceil(Rate * 0.04));
        this.EchoTone = [new OnePole(Rate, 4800), new OnePole(Rate, 4800)];
        this.Taps = [[0.0071, 0.0173, 0.0299], [0.0097, 0.0211, 0.0337]].map((Side) => Side.map((Seconds) => Seconds * Rate));
        this.Count = 0;
    }

    Feed({ Motors = [], Throttle = 0, Gear, Clunk, Speed = 0, Ready = 0 })
    {
        Motors.forEach((Feed, Index) => { const Unit = this.Units[Index]; if (Unit) { Unit.Rpm.Target = Math.max(0, Feed.Rpm); Unit.Load.Target = Clamp(Feed.Load, -1, 1); } });
        this.Throttle.Target = Clamp(Throttle, 0, 1);
        this.Speed.Target = Speed;
        if (Gear !== undefined) this.Gear = Gear;
        if (Clunk !== undefined && Clunk !== this.Clunk) { this.ClunkHit = 1; this.Clunk = Clunk; }
        if (Ready) this.Chime = 1;
    }

    Render(Left, Right, Offset = 0, Count = Left.length - Offset)
    {
        const P = this.Profile, V = P.Voice, Rate = this.Rate, Noise = this.Noise, Nyquist = Rate * 0.45;
        const Fade = (Hz) => (Hz < 20 ? 0 : 1 - Smoothstep(Nyquist * 0.75, Nyquist, Hz));
        for (let Index = Offset; Index < Offset + Count; Index++)
        {
            const Throttle = this.Throttle.Run(), Speed = this.Speed.Run();
            let Mid = 0, Side = 0;

            for (const Unit of this.Units)
            {
                const Motor = Unit.Motor, Rpm = Unit.Rpm.Run(), Load = Unit.Load.Run(), Drive = Math.abs(Load);
                const Shaft = Rpm / 60, Electrical = Shaft * Motor.PolePairs, Present = Smoothstep(30, 900, Rpm);
                if ((this.Count + Unit.Tune) % 256 === 0)
                {
                    for (let Order = 0; Order < Unit.Drift.length; Order++) Unit.Drift[Order] = Clamp(Unit.Drift[Order] + Noise.Gauss() * 0.08, 0.6, 1.3);
                    const Hz = Clamp(900 + Rpm * 0.22, 900, 7000);
                    Unit.Windage[0].Shape('Band', Hz, 1.4); Unit.Windage[1].Shape('Band', Hz * 2.3, 2.2);
                }

                // Electromagnetic orders: torque ripple and slot harmonics; regeneration is a little softer and darker.
                // Rotor eccentricity modulates everything at the shaft rate (sidebands ± 1 × shaft: the slight roughness of
                // a real motor); torque ripple grows with current, so the high orders come up under load.
                Unit.Shaft += Shaft / Rate; Unit.Shaft -= Math.floor(Unit.Shaft);
                const Eccentric = 1 + 0.12 * Math.sin(Tau * Unit.Shaft) + 0.05 * Math.sin(Tau * 2 * Unit.Shaft + 0.9);
                const Push = (0.12 + 0.88 * Math.pow(Drive, 0.8)) * (Load < 0 ? 0.75 : 1) * Present;
                let Whine = 0;
                Motor.Orders.forEach(([Order, Level], Slot) =>
                {
                    const Hz = Shaft * Order, Strain = Order > 24 ? 0.45 + 0.75 * Drive : 1;
                    Whine += Unit.Orders[Slot].Run(Hz) * Level * Strain * Unit.Drift[Slot] * Fade(Hz) * (Load < 0 && Order > 30 ? 0.6 : 1);
                });
                Whine *= Push * V.Whine * 1.15 * Motor.Level * Eccentric;

                // Inverter: carrier and sidebands, louder with current, fading as the motor speeds up (masking + modulation).
                const Current = Math.pow(Drive, 0.7) * (1 - 0.65 * Smoothstep(1500, 9000, Rpm)) * (this.Gear > 0 || Drive > 0.02 ? 1 : 0);
                let Inverter = 0;
                if (Current > 0.001)
                {
                    const Carrier = Motor.Carrier;
                    const Pairs = [[Carrier, 0.35], [Carrier - 2 * Electrical, 1], [Carrier + 2 * Electrical, 1], [Carrier - 4 * Electrical, 0.4], [Carrier + 4 * Electrical, 0.4]];
                    for (let Slot = 0; Slot < 5; Slot++) { const Hz = Math.abs(Pairs[Slot][0]); Inverter += Unit.Sides[Slot].Run(Hz) * Pairs[Slot][1] * Fade(Hz); }
                    // Spread-spectrum switching: the carrier is dithered, so a hiss band sits around it under the tones.
                    if ((this.Count + Unit.Tune) % 64 === 0) Unit.Spread.Shape('Band', Carrier, 5);
                    Inverter += Unit.Spread.Run(Noise.White()) * 2.2 * Fade(Carrier);
                    Inverter *= Current * V.Inverter * 1.3 * Motor.Level;
                }

                // Gear mesh whine for the engaged gear (the racer's straight-cut gears whine on the overrun too).
                const Gear = Clamp(Motor.Meshes.length > 1 ? this.Gear || 1 : 1, 1, Motor.Meshes.length) - 1;
                let Mesh = 0;
                Motor.Meshes[Gear].forEach(([Order, Level], Slot) => { const Hz = Shaft * Order; Mesh += Unit.Meshes[Gear][Slot].Run(Hz) * Level * Fade(Hz); });
                // Gear run-out: the mesh is modulated once per pinion turn (sidebands), harder under load.
                Mesh *= (0.3 + 0.7 * Math.pow(Drive, 0.8)) * Present * Smoothstep(200, 3500, Rpm) * V.Gears * 1.3 * Motor.Level * (1 + 0.2 * Drive * Math.sin(Tau * Unit.Shaft + 2.1));

                const Spin = Rpm / P.MaxRpm, Air = Noise.White();
                const Windage = (Unit.Windage[0].Run(Air) + Unit.Windage[1].Run(Air) * 0.5) * Spin * Spin * V.Windage * 2;
                const Voice = Whine + Inverter + Mesh + Windage;
                Mid += Voice; Side += Voice * Motor.Pan;
            }

            // Lead motor (boxed one): drives the racer's whir and undertone and the sport layer.
            const Lead = this.Units[this.Units.length - 1], LeadRpm = Lead.Rpm.Current, LeadLoad = Lead.Load.Current;
            if (this.Sport > 0 && P.Sport)                                   // only cars with a sport-sound tuning (the racer has its whir)
            {
                // Sport sound (synthetic, like the optional sound packs EVs play through speakers): a deep tone that rises
                // with road speed, a fifth and an octave above it, opening up with throttle.
                const Base = Mix(P.Sport.Low, P.Sport.High, Math.pow(Clamp(Speed * 3.6 / P.TopKmh, 0, 1), 0.75)) * (1 + 0.08 * Math.max(0, LeadLoad));
                if ((this.Count & 63) === 0) this.SportTone.Shape('Low', 280 + P.Sport.Bright * Math.max(Throttle, 0.15), 1.1);
                let Pad = 0;
                [[1, 1], [1.5, 0.5], [2, 0.45], [0.5, 0.6]].forEach(([Ratio, Level], Slot) =>
                {
                    const Phase = this.SportTones[Slot].Run(Base * Ratio * (1 + Slot * 0.0021), Rate);
                    Pad += (Phase + 0.33 * Math.sin(Tau * 2 * this.SportTones[Slot].Phase) + 0.18 * Math.sin(Tau * 3 * this.SportTones[Slot].Phase)) * Level;
                });
                Mid += this.SportTone.Run(Pad) * this.Sport * (0.35 + 0.65 * Throttle) * Smoothstep(0, 1.5, Speed + Throttle * 2);
            }
            if (V.Undertone > 0)
            {
                // V8 undertone: cross-plane firing (each bank uneven), low-passed into a chest-deep thrum under the whir.
                const U = this.Undertone, Crank = LeadRpm * P.Undertone.Ratio + 900;
                U.Crank = (U.Crank + Crank * 6 / Rate) % 720;
                const Slot = Math.floor(U.Crank / 90);
                if (Slot !== U.Slot) { U.Slot = Slot; U.Amp = [1, 0.78, 0.94, 0.72, 1.06, 0.8, 0.9, 0.84][Slot] * (0.85 + 0.3 * Noise.Range(0, 1)); }
                const Age = (U.Crank - Slot * 90) / 90;
                const Pulse = U.Amp * (Age < 0.12 ? Math.sin(Math.PI * Age / 0.24) : Math.exp(-(Age - 0.12) * 5)) - 0.3;
                U.Level += ((0.3 + 0.7 * Math.max(Throttle, Math.max(0, LeadLoad))) - U.Level) * 0.0005;
                Mid += U.Chest.Run(U.Body.Run(Pulse)) * V.Undertone * U.Level * 0.25 * (this.Gear > 0 || Throttle > 0 ? 1 : 0.4);
                this.Growl = Clamp(Pulse, -0.3, 1);                           // the whir is roughened by the same firing pulses
            }

            if (V.Whir > 0)
            {
                // The racer's whir (Follow-up 20): band-limited saws (root, fifth and octave) locked to the motor through
                // two formants that track the pitch, so it screams like a turbine-electric hybrid rather than an
                // organ; a breathy narrow band an octave up, a slow pitch drift with a little vibrato, and the V8
                // undertone's firing pulses roughening it (one machine, not two layers). Rises through each gear and
                // drops on every upshift.
                const W = P.Whir, Root = LeadRpm / 60 * W.Order;
                this.WhirPhase += W.Vibrato / Rate;
                const Wobble = 1 + 0.003 * Math.sin(Tau * this.WhirPhase) + 0.004 * this.WhirDrift.Run();
                let Whir = 0;
                [[1, 1], [W.Fifth * 1.002, 0.55], [2, 0.3]].forEach(([Ratio, Level], Slot) => { const Hz = Root * Ratio * Wobble; Whir += this.WhirSaws[Slot].Run(Hz, Rate) * Level * Fade(Hz * 3); });
                if ((this.Count & 63) === 0)
                {
                    this.WhirBand.Shape('Band', Clamp(Root * 1.4, 300, 9000), 0.7); this.TurbineBand.Shape('Band', Clamp(2500 + LeadRpm * 0.35, 2500, 9500), 2.5);
                    this.WhirFormants[0].Shape('Band', Clamp(Root * 2.2, 400, 12000), 2.2); this.WhirFormants[1].Shape('Band', Clamp(Root * 4.6, 800, 15000), 3);
                }
                const Drive = 0.25 + 0.75 * Math.pow(Math.max(0, LeadLoad), 0.7) + 0.15 * Math.max(0, -LeadLoad);
                const Rough = 1 + 0.35 * this.Growl * Math.max(Throttle, 0.3);
                const Voiced = this.WhirBand.Run(Whir) * 1.1 + this.WhirFormants[0].Run(Whir) * 1.3 + this.WhirFormants[1].Run(Whir) * 0.8 + Whir * 0.12 + this.WhirBreath.Run(Root * 2 * Wobble) * 0.35 * Fade(Root * 2);
                Mid += Voiced * V.Whir * 1.45 * Drive * Rough * Smoothstep(100, 1500, LeadRpm);
                Mid += this.TurbineBand.Run(Noise.White()) * V.Whir * 0.6 * Math.pow(LeadRpm / P.MaxRpm, 1.5);
            }
            // Shift thunk and the ready chime.
            let Extra = 0;
            if (this.ClunkHit > 0) { Extra += this.ClunkRing.Run(this.ClunkHit * (Noise.White() * 0.5 + 0.5)) * P.Clunk.Gain; this.ClunkHit = this.ClunkHit > 0.02 ? this.ClunkHit * 0.6 : 0; }
            else Extra += this.ClunkRing.Run(0) * P.Clunk.Gain;
            if (this.Chime > 0.0005)
            {
                const Age = 1 - this.Chime, Note = Age < 0.18 ? 0 : 1;
                Extra += this.ChimeTones[Note].Run(P.Chime[Note], Rate) * 0.05 * this.Chime * Smoothstep(0, 0.01, Age - (Note ? 0.18 : 0));
                this.Chime *= 1 - 1 / (0.45 * Rate);
            }
            Mid += Extra;

            // Pickup.
            let Left0 = Mid + Side, Right0 = Mid - Side;
            if (this.Pickup === 'Cabin') { Left0 = this.Cabin[0].Run(Left0) * 1.3; Right0 = this.Cabin[1].Run(Right0) * 1.3; }
            else if (this.Pickup === 'Chase' && P.Space)
            {
                this.Echo.Write((Left0 + Right0) * 0.5);
                const [TapsL, TapsR] = this.Taps;
                Left0 += this.EchoTone[0].Run(this.Echo.Read(TapsL[0]) * 0.34 + this.Echo.Read(TapsL[1]) * 0.22 + this.Echo.Read(TapsL[2]) * 0.14) * P.Space * 2;
                Right0 += this.EchoTone[1].Run(this.Echo.Read(TapsR[0]) * 0.34 + this.Echo.Read(TapsR[1]) * 0.22 + this.Echo.Read(TapsR[2]) * 0.14) * P.Space * 2;
            }
            else if (this.Pickup === 'Dry') { Left0 = Right0 = Mid; }
            Left[Index] += SoftClip(this.Air[0].Run(this.Blocks[0].Run(Left0)), 0.8);
            Right[Index] += SoftClip(this.Air[1].Run(this.Blocks[1].Run(Right0)), 0.8);
            this.Count++;
        }
    }
}
