// FORCED INDUCTION
// 🧩 Turbochargers and superchargers as swappable add-ons for any combustion car on the bench: each model changes the
//    torque curve (how much boost, how early, how much lag) and has its own voice, rendered by InductionVoice on top of
//    the engine. Six turbos (small single, big single, parallel twins, a turbo without a blow-off valve, variable
//    geometry, electric assist) and four superchargers (classic Roots, twisted-rotor 4-lobe, twin-screw, centrifugal).
// 📝 InductedProfile(Base, Name) returns a car profile with the new torque curve (for turbos: a silent turbine model so
//    TractionIntegrator computes boost with the model's lag and PulseTrainEngine softens the exhaust through it). The
//    supercharger whines are rotor orders of the crank speed (pulley ratio × lobe pockets), so they track the engine
//    exactly; turbo whistles follow the shaft speed, which follows boost. Pulley ratios, lobe counts and whistle
//    ranges are typical values, not measurements of a particular product.

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, OnePole, Biquad, Resonator, DcBlock, Slew, SoftClip, NarrowTone, PolySaw } from './SignalKit.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    MODELS
//------------------------------------------------------------------------------------------------------------------------

/**
 * Turbo fields: Gain (full-boost torque ÷ unboosted), Onset / Full (rpm where boost starts / is full), Lag (s),
 * Whistle [Hz at idle shaft, Hz at full shaft], Twins (1 or 2 whistles), Tone, Spool [Hz] band and Whoosh level,
 * Valve ('Atmos' vents loudly to air, 'Recirc' recirculates softly, 'None'), Vent (blow-off level), Flutter (surge
 * chatter level), Muffle (exhaust low-pass through the turbine at full shaft speed), Vanes (VGT hiss), Assist (e-motor).
 * Supercharger fields: Gain, Curve ('Flat' positive displacement, 'Square' centrifugal), Ratio (pulley, rotor ÷ crank),
 * Pockets (pressure pulses per rotor turn), Harmonics [[multiple, level]], Whine, Pulsing (Roots chug), Bypass
 * (valve that unloads the rotors off throttle), TimingGear (teeth), StepUp / Blades / GearTeeth (centrifugal).
 */
export const Inducers = {
    SmallSingle: {
        Label: 'Small single turbo', Note: 'Hot-hatch size: quick spool, bright whistle, soft recirculating blow-off', Family: 'Turbo',
        Gain: 1.55, Onset: 1900, Full: 3100, Lag: 0.3, Whistle: [6500, 13500], Twins: 1, Tone: 0.032, Spool: [1500, 5200], Whoosh: 0.1,
        Valve: 'Recirc', Vent: 1.17, Flutter: 0, Muffle: 3200,
    },
    BigSingle: {
        Label: 'Big single turbo', Note: 'Big-power build: late, violent boost, deep whoosh, loud atmospheric blow-off', Family: 'Turbo',
        Gain: 2.1, Onset: 3600, Full: 5200, Lag: 1.0, Whistle: [2600, 7400], Twins: 1, Tone: 0.048, Spool: [450, 2300], Whoosh: 0.208,
        Valve: 'Atmos', Vent: 1.2, Flutter: 0.1, Muffle: 1800,
    },
    TwinTurbo: {
        Label: 'Parallel twin turbos', Note: 'One per bank: two whistles beating, moderate lag, blow-off with a little flutter', Family: 'Turbo',
        Gain: 1.8, Onset: 2200, Full: 3600, Lag: 0.5, Whistle: [4200, 9000], Twins: 2, Detune: 1.013, Tone: 0.044, Spool: [900, 3400], Whoosh: 0.14,
        Valve: 'Atmos', Vent: 0.7, Flutter: 0.35, Muffle: 2400,
    },
    Flutter: {
        Label: 'Turbo, no blow-off valve', Note: 'The air surges back through the compressor on every lift: stu-tu-tu chatter', Family: 'Turbo',
        Gain: 1.9, Onset: 3000, Full: 4500, Lag: 0.75, Whistle: [3200, 8000], Twins: 1, Tone: 0.04, Spool: [700, 2800], Whoosh: 0.144,
        Valve: 'None', Vent: 0, Flutter: 1.2, Muffle: 2100,
    },
    Vgt: {
        Label: 'Variable-geometry turbo', Note: 'Diesel-style: boost from low rpm, whistle sweeping with the vanes, whoosh on lift, no valve', Family: 'Turbo',
        Gain: 1.7, Onset: 1300, Full: 2400, Lag: 0.4, Whistle: [2400, 11500], Twins: 1, Tone: 0.06, Spool: [1200, 6000], Whoosh: 0.12,
        Valve: 'None', Vent: 0, Flutter: 0, Vanes: 0.05, Muffle: 2600,
    },
    ElectricTurbo: {
        Label: 'Electric-assist turbo', Note: 'A motor spins the shaft before the exhaust can: no lag, electric whine, harvest whine on lift', Family: 'Turbo',
        Gain: 1.8, Onset: 1200, Full: 2300, Lag: 0.12, Whistle: [5000, 12000], Twins: 1, Tone: 0.036, Spool: [1000, 4500], Whoosh: 0.1,
        Valve: 'Recirc', Vent: 1.04, Flutter: 0, Assist: { PolePairs: 2, MaxRpm: 110000, Level: 0.044 }, Muffle: 2500,
    },
    Roots: {
        Label: 'Roots blower', Note: 'Classic straight 3-lobe rotors on top of the engine: gear-driven whine with a chugging pulse', Family: 'Supercharger',
        Gain: 1.6, Curve: 'Flat', Ratio: 1.25, Pockets: 6, Harmonics: [[1, 1], [2, 0.65], [3, 0.4], [4, 0.22], [0.5, 0.25]], Whine: 0.16,
        Pulsing: 0.55, Bypass: false, Rattle: 0.05, Intake: 0.1,
    },
    TwistedLobe: {
        Label: 'Twisted 4-lobe supercharger', Note: 'Modern muscle-car blower: high smooth scream at full throttle, bypass valve quietens it on the cruise', Family: 'Supercharger',
        Gain: 1.75, Curve: 'Flat', Ratio: 2.4, Pockets: 8, Harmonics: [[1, 1], [2, 0.3], [3, 0.1]], Whine: 0.147,
        Pulsing: 0.12, Bypass: true, Rattle: 0, Intake: 0.05,
    },
    TwinScrew: {
        Label: 'Twin-screw supercharger', Note: 'Meshing screw rotors: sharp raspy scream rich in harmonics, timing-gear whine, bypass valve', Family: 'Supercharger',
        Gain: 1.8, Curve: 'Flat', Ratio: 2.9, Pockets: 4, Harmonics: [[1, 1], [2, 0.75], [3, 0.55], [4, 0.4], [5, 0.28], [1.5, 0.22]], Whine: 0.135,
        Pulsing: 0.25, Bypass: true, TimingGear: 23, Rattle: 0, Intake: 0.06,
    },
    DemonScrew: {
        // The SRT Demon's own blower: 2.7 L per rev IHI-type twin-screw on the 6.2 L HEMI, 14.5 psi (Dodge press kit).
        // Drive ratio and lobe count are typical for this blower family, not Dodge data.
        Label: '2.7 L twin-screw (SRT Demon)', Note: 'The Demon\'s stock blower, 14.5 psi: the Hellcat-family scream, bypass valve on the cruise', Family: 'Supercharger',
        Gain: 1.73, Curve: 'Flat', Ratio: 2.36, Pockets: 4, Harmonics: [[1, 1], [2, 0.7], [3, 0.5], [4, 0.32], [5, 0.2], [1.5, 0.18]], Whine: 0.15,
        Pulsing: 0.22, Bypass: true, TimingGear: 25, Rattle: 0, Intake: 0.08,
    },
    Centrifugal: {
        Label: 'Centrifugal supercharger', Note: 'Belt-driven impeller: whine and boost climb with rpm squared, gear-drive zing, blow-off on lift', Family: 'Supercharger',
        Gain: 1.75, Curve: 'Square', Ratio: 3.1, StepUp: 3.6, Blades: 6, GearTeeth: 16, Whine: 0.12, Valve: 'Atmos', Vent: 0.8, Intake: 0.07,
    },
};

/** A car profile re-tuned for an inducer (Base may be naturally aspirated or already turbocharged). */
export function InductedProfile(Base, Name)
{
    const Model = Inducers[Name];
    if (!Model) return Base;
    const Natural = Base.Turbo ? Base.Turbo.Unboosted : Base.Stock ? 1 / Inducers[Base.Stock].Gain : 1;   // share of the base table made without boost
    const Unboosted = Base.Torque.map(([Rpm, Torque]) => [Rpm, Torque * Natural]);
    const Profile = { ...Base, Name: `${Base.Name} + ${Model.Label}`, Induction: Name };
    if (Model.Family === 'Turbo')
    {
        Profile.Torque = Unboosted.map(([Rpm, Torque]) => [Rpm, Torque * Model.Gain]);
        Profile.Turbo = { Onset: Model.Onset, Full: Model.Full, Lag: Model.Lag, Unboosted: 1 / Model.Gain, Whistle: Model.Whistle, Tone: 0, Spool: 0, Vent: 0, Flutter: 0, Muffle: Model.Muffle, Assist: !!Model.Assist };
    }
    else
    {
        const Boost = (Rpm) => (Model.Curve === 'Square' ? Math.pow(Clamp(Rpm / Base.Redline, 0, 1.1), 2) : Smoothstep(600, 2200, Rpm));
        Profile.Torque = Unboosted.map(([Rpm, Torque]) => [Rpm, Torque * (1 + (Model.Gain - 1) * Boost(Rpm))]);
        Profile.Turbo = null;
    }
    return Profile;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VOICE
//------------------------------------------------------------------------------------------------------------------------

/** Pink-ish noise (white through a three-pole mix) for air: a whoosh made from white noise sounds like hiss. */
class Air
{
    constructor(Rate, Seed) { this.Noise = new SeededNoise(Seed); this.Poles = [new OnePole(Rate, 80), new OnePole(Rate, 900), new OnePole(Rate, 6000)]; }
    Run() { const White = this.Noise.White(); return this.Poles[0].Run(White) * 3.2 + this.Poles[1].Run(White) * 1.1 + this.Poles[2].Run(White) * 0.5 + White * 0.08; }
}

/**
 * A turbo compressor as an aero-acoustic source (Follow-up 20). The whistle is the blade-pass tone of the compressor
 * wheel (6 full + 6 splitter blades: Pitch = 12 × shaft rev/s) as a narrow band of turbulent noise around the pitch
 * (NarrowTone), not a sine; the full blades alone add a weaker tone at half the pitch; shaft imbalance modulates it at
 * the shaft rate (sidebands, the slightly rough "sizzle" of a real turbo); near full load the blade tips go supersonic
 * and add a buzz of shaft orders; and the inlet air rush is pink noise through the intake duct's broad resonances,
 * decorrelated left and right. Run(Pitch, Shaft, Throttle, Whoosh) → [Mid, Side].
 */
export class Compressor
{
    constructor(Rate, { Twins = 1, Detune = 1.013, Seed = 41, Spool = [900, 3400] } = {})
    {
        this.Rate = Rate; this.Twins = Twins; this.Detune = Detune; this.Spool = Spool;
        this.Blade = [0, 1].map((Twin) => new NarrowTone(Rate, { Breath: 0.42, Width: 0.009, Jitter: 0.003, Flutter: 0.22, Seed: Seed + Twin * 17 }));
        this.Half = new NarrowTone(Rate, { Breath: 0.6, Width: 0.014, Jitter: 0.003, Flutter: 0.3, Seed: Seed + 3 });
        this.Double = new NarrowTone(Rate, { Breath: 0.65, Width: 0.012, Jitter: 0.003, Flutter: 0.3, Seed: Seed + 5 });
        this.Buzz = new PolySaw(); this.BuzzBand = new Biquad(Rate);
        this.Airs = [new Air(Rate, Seed + 7), new Air(Rate, Seed + 9)];
        this.Duct = [0, 1].map(() => [new Biquad(Rate), new Biquad(Rate), new Biquad(Rate).Shape('High', 160, 0.7)]);
        this.ShaftPhase = 0; this.Count = 0;
    }

    Run(Pitch, Shaft, Throttle, Whoosh = 0.1)
    {
        const Rate = this.Rate, Fade = (Hz) => 1 - Smoothstep(Rate * 0.34, Rate * 0.45, Hz), ShaftHz = Pitch / 12;
        if ((this.Count++ & 63) === 0)
        {
            const Hz = Mix(this.Spool[0], this.Spool[1], Shaft);
            for (const Duct of this.Duct) { Duct[0].Shape('Band', Hz, 0.75); Duct[1].Shape('Band', Hz * 2.3, 1.1); }
            this.BuzzBand.Shape('Band', Clamp(Pitch * 0.55, 400, 9000), 1.4);
        }
        this.ShaftPhase += ShaftHz / Rate; this.ShaftPhase -= Math.floor(this.ShaftPhase);
        const Wobble = 1 + 0.2 * Math.sin(Tau * this.ShaftPhase) + 0.08 * Math.sin(Tau * 2 * this.ShaftPhase + 1.3);
        let Mid = 0, Side = 0;
        const A = this.Blade[0].Run(Pitch) * Fade(Pitch) * Wobble;
        if (this.Twins === 2) { const B = this.Blade[1].Run(Pitch * this.Detune) * Fade(Pitch) * (2 - Wobble); Mid += (A + B) * 0.5; Side += (A - B) * 0.35; }
        else Mid += A;
        Mid += this.Half.Run(Pitch * 0.5) * 0.3 + this.Double.Run(Pitch * 2) * 0.12 * Fade(Pitch * 2);
        Mid += this.BuzzBand.Run(this.Buzz.Run(ShaftHz, Rate)) * 0.9 * Math.pow(Shaft, 4) * Throttle;
        // Inlet rush: scaled by the caller (Whoosh); returned separately so callers can level it against the whistle.
        const Flow = Whoosh * Shaft * (0.2 + 0.8 * Throttle) * 2.9;
        const RushL = this.Duct[0][2].Run(this.Duct[0][0].Run(this.Airs[0].Run()) + this.Duct[0][1].Run(this.Airs[0].Run()) * 0.4);
        const RushR = this.Duct[1][2].Run(this.Duct[1][0].Run(this.Airs[1].Run()) + this.Duct[1][1].Run(this.Airs[1].Run()) * 0.4);
        return [Mid, Side, (RushL + RushR) * 0.5 * Flow, (RushL - RushR) * 0.5 * Flow];
    }
}

/**
 * Blow-off valve. Fire(Pressure) when the throttle snaps shut on boost. Atmospheric: a sharp "ch" as the piston
 * cracks open, then a jet of air whose hiss falls in pitch as the pressure bleeds away ("pssshh"), the piston
 * fluttering on its spring as it closes; recirculating: the same through the intake, so a duller, shorter "tshh".
 * Run() → [Mid, Side].
 */
export class BlowOff
{
    constructor(Rate, Vents = 'Atmos', Seed = 51)
    {
        this.Rate = Rate; this.Open = Vents === 'Atmos'; this.Noise = new SeededNoise(Seed); this.Side = new SeededNoise(Seed + 1);
        this.Jet = [new Biquad(Rate), new Biquad(Rate)]; this.Hiss = [new Biquad(Rate).Shape('High', this.Open ? 3000 : 1200, 0.7), new Biquad(Rate).Shape('High', this.Open ? 3000 : 1200, 0.7)];
        this.Dull = [new Biquad(Rate).Shape('Low', this.Open ? 11000 : 3400, 0.7), new Biquad(Rate).Shape('Low', this.Open ? 11000 : 3400, 0.7)];
        this.Pressure = 0; this.Start = 0; this.Age = 0; this.Count = 0;
    }

    Fire(Pressure) { this.Pressure = this.Start = Pressure; this.Age = 0; }
    get Busy() { return this.Pressure > 0.15 * this.Start && this.Pressure > 0.02; }

    Run()
    {
        if (this.Pressure < 0.0005) return [0, 0];
        const Rate = this.Rate, T = this.Age++ / Rate, Share = this.Pressure / Math.max(0.05, this.Start);
        if ((this.Count++ & 31) === 0)
        {
            const Hz = this.Open ? Mix(1900, 5200, Share) : Mix(800, 2200, Share);
            this.Jet[0].Shape('Band', Hz, 0.8); this.Jet[1].Shape('Band', Hz * 1.06, 0.8);
        }
        const Crack = T < 0.004 ? (1 - T / 0.004) * 1.8 : 0;
        const Attack = Math.min(1, T / 0.006);
        const Flutter = this.Open && Share < 0.55 ? 1 - 0.45 * Math.pow(Math.max(0, Math.sin(Tau * 34 * T)), 2) : 1;
        const Level = this.Pressure * Attack * Flutter;
        const L = this.Dull[0].Run(this.Jet[0].Run(this.Noise.White()) * 1.6 + this.Hiss[0].Run(this.Noise.White()) * 0.5) * Level + this.Noise.White() * Crack * this.Start * 0.4;
        const R = this.Dull[1].Run(this.Jet[1].Run(this.Side.White()) * 1.6 + this.Hiss[1].Run(this.Side.White()) * 0.5) * Level;
        this.Pressure *= 1 - 1 / ((this.Open ? 0.34 : 0.15) * Rate);
        return [(L + R) * 0.5, (L - R) * 0.4];
    }
}

/**
 * Compressor surge (no blow-off valve): trapped air reverses through the wheel in bursts, each a chuff of turbulent air
 * ringing the intake pipe, the bursts slowing from about 20 to 7 a second as the pressure falls: "stu-tu-tu-tu".
 */
export class Surge
{
    constructor(Rate, Seed = 61)
    {
        this.Rate = Rate; this.Noise = new SeededNoise(Seed);
        this.Bands = [new Biquad(Rate).Shape('Band', 750, 1.3), new Biquad(Rate).Shape('Band', 1650, 2), new Biquad(Rate).Shape('Band', 3900, 1.2)];
        this.Pipe = new Resonator(Rate, 1080, 0.035);
        this.Level = 0; this.Phase = 0; this.Chuff = 0; this.Decay = Math.exp(-1 / (0.016 * Rate));
    }

    Fire(Level) { this.Level = Level; this.Phase = 0.95; }
    get Busy() { return this.Level > 0.15; }

    Run()
    {
        if (this.Level < 0.0005) return 0;
        this.Phase += Mix(7, 20, this.Level) / this.Rate;
        if (this.Phase >= 1) { this.Phase -= 1; this.Chuff = this.Noise.Range(0.65, 1); }
        this.Chuff *= this.Decay;
        const Air = this.Noise.White() * this.Chuff;
        const Out = this.Bands[0].Run(Air) * 1.2 + this.Bands[1].Run(Air) * 0.7 + this.Bands[2].Run(Air) * 0.25 + this.Pipe.Run(Air) / this.Pipe.Norm * 0.01;
        this.Level *= 1 - 1 / (0.55 * this.Rate);
        return Out * this.Level;
    }
}

/**
 * The inducer's own sound. Feed({ Rpm, Throttle, Boost, Spool }) each block (Boost and shaft Spool from TractionIntegrator for turbos; the
 * superchargers work out theirs from rpm), then Render() adds stereo samples. Lifting off sharply (throttle falling
 * well below its slow average) with boost up fires the blow-off valve or the surge chatter.
 * Follow-up 20: every tone is a narrow band of noise (NarrowTone) with drift and flutter; the displacement blowers
 * are pressure pulses (every rotor pocket opening to the outlet), with a rotor-rate modulation and a turbulent rasp
 * gated by each pocket, so they growl and scream instead of humming like an oscillator.
 */
export class InductionVoice
{
    constructor(Rate, Name, { Pickup = 'Chase', Seed = 31, Redline = 7000 } = {})
    {
        this.Rate = Rate; this.Name = Name; this.Model = Inducers[Name]; this.Pickup = Pickup; this.Redline = Redline;
        const M = this.Model;
        this.Noise = new SeededNoise(Seed);
        this.Rpm = new Slew(Rate, 0.012, 0); this.Throttle = new Slew(Rate, 0.03, 0); this.Held = new Slew(Rate, 0.18, 0);
        this.Boost = new Slew(Rate, 0.05, 0); this.FedBoost = 0; this.FedSpool = null;
        this.Shaft = new Slew(Rate, M.Lag ? Mix(0.18, 0.6, Clamp(M.Lag, 0, 1)) : 0.2, 0);
        if (M.Family === 'Turbo') this.Wheel = new Compressor(Rate, { Twins: M.Twins, Detune: M.Detune ?? 1.013, Seed: Seed + 1, Spool: M.Spool });
        // Narrow tones: displacement harmonics get breathier as they go up; slot 6 = timing gear, 7 = e-motor.
        this.Tones = Array.from({ length: 8 }, (_, Slot) => new NarrowTone(Rate, { Breath: Math.min(0.75, 0.28 + Slot * 0.08), Width: 0.006 + Slot * 0.002, Jitter: 0.002, Flutter: 0.15, Seed: Seed + 20 + Slot }));
        this.Motor = [new NarrowTone(Rate, { Breath: 0.12, Width: 0.004, Jitter: 0.001, Flutter: 0.08, Seed: Seed + 40 }), new NarrowTone(Rate, { Breath: 0.25, Width: 0.006, Jitter: 0.001, Flutter: 0.1, Seed: Seed + 41 })];
        this.Bands = Array.from({ length: 6 }, () => new Biquad(Rate));
        this.Valve = new BlowOff(Rate, M.Valve === 'Recirc' ? 'Recirc' : 'Atmos', Seed + 50);
        this.Stall = new Surge(Rate, Seed + 60);
        this.Intake = new Air(Rate, Seed + 70); this.IntakeBand = new Biquad(Rate).Shape('Band', 700, 0.9);
        this.Rasp = new Biquad(Rate); this.Pocket = 0; this.RotorPhase = 0; this.Rattle = 0; this.RattlePhase = 0;
        this.Cabin = new Biquad(Rate).Shape('Low', 5000, 0.7);
        this.Blocks = [new DcBlock(Rate, 20), new DcBlock(Rate, 20)];
        this.Count = 0;
    }

    Feed({ Rpm = 0, Throttle = 0, Boost, Spool })
    {
        this.Rpm.Target = Math.max(0, Rpm);
        this.Throttle.Target = Clamp(Throttle, 0, 1);
        if (Boost !== undefined) this.FedBoost = Clamp(Boost, 0, 1.2);
        if (Spool !== undefined) this.FedSpool = Clamp(Spool, 0, 1.1);
    }

    Render(Left, Right, Offset = 0, Count = Left.length - Offset)
    {
        const M = this.Model, Rate = this.Rate, Noise = this.Noise, Fade = (Hz) => 1 - Smoothstep(Rate * 0.34, Rate * 0.45, Hz);
        for (let Index = Offset; Index < Offset + Count; Index++)
        {
            const Rpm = this.Rpm.Run(), Throttle = this.Throttle.Run(), Held = this.Held.Run(); this.Held.Target = Throttle;
            const Tune = (this.Count++ & 63) === 0;
            let Mid = 0, Side = 0, Boost = 0;

            if (M.Family === 'Turbo')
            {
                this.Boost.Target = this.FedBoost; Boost = this.Boost.Run();
                let Spin = Math.sqrt(Boost) * 0.85 + 0.15 * Smoothstep(800, 6000, Rpm);
                if (M.Assist && Throttle > 0.1) Spin = Math.max(Spin, 0.35 + 0.35 * Throttle);   // the e-motor pre-spins the shaft
                this.Shaft.Target = Spin;
                const Shaft = this.FedSpool !== null ? (this.Shaft.Current += (Math.max(this.FedSpool, M.Assist ? Spin : 0) - this.Shaft.Current) * 0.002) : this.Shaft.Run();

                // Compressor: blade-pass whistle (the VGT's vanes sweep it with boost) and the inlet rush.
                const Pitch = Mix(M.Whistle[0], M.Whistle[1], M.Vanes ? Math.pow(Shaft, 0.7) * Mix(0.8, 1.1, Boost) : Shaft);
                const Level = M.Tone * 2.3 * Shaft * Shaft * (0.35 + 0.65 * Throttle);
                const [WhistleMid, WhistleSide, RushMid, RushSide] = this.Wheel.Run(Pitch, Shaft, Throttle, M.Whoosh);
                Mid += WhistleMid * Level + RushMid; Side += WhistleSide * Level + RushSide;
                if (Tune && M.Vanes) this.Bands[2].Shape('Band', Mix(3000, 9000, Shaft), 3);
                if (M.Vanes) Mid += this.Bands[2].Run(Noise.White()) * M.Vanes * Throttle * Shaft;

                // Electric assist: motor whine at the shaft's electrical frequency; on a lift it keeps singing as it harvests.
                if (M.Assist)
                {
                    const Electrical = Mix(15000, M.Assist.MaxRpm, Shaft) / 60 * M.Assist.PolePairs;
                    const Drive = Math.max(Throttle * (1 - Boost * 0.6), (Held - Throttle) * 1.5, 0.12 * Shaft);
                    Mid += (this.Motor[0].Run(Electrical) * Fade(Electrical) + this.Motor[1].Run(Electrical * 3) * 0.35 * Fade(Electrical * 3)) * M.Assist.Level * Drive;
                }
            }
            else
            {
                // Superchargers: boost is immediate (flat for displacement types, rpm² for centrifugal).
                const Top = this.Redline;
                Boost = M.Curve === 'Square' ? Math.pow(Clamp(Rpm / Top, 0, 1.1), 2) * Mix(0.25, 1, Throttle) : Smoothstep(600, 2200, Rpm) * Mix(M.Bypass ? 0.1 : 0.45, 1, Throttle);
                this.Boost.Target = Boost; Boost = this.Boost.Run();
                const Rotor = Rpm * M.Ratio;                                       // rotor or pulley rpm
                const Load = M.Bypass ? Mix(0.22, 1, Smoothstep(0.15, 0.7, Throttle)) : Mix(0.55, 1, Throttle);
                const Speed = Smoothstep(300, 1200, Rpm) * (0.35 + 0.65 * Clamp(Rpm / Top, 0, 1.2));
                this.RotorPhase += Rotor / 60 / Rate; this.RotorPhase -= Math.floor(this.RotorPhase);

                if (M.Curve !== 'Square')
                {
                    // Displacement blowers: each rotor pocket opening to the outlet is a pressure pulse (backflow), so
                    // the whine is a pulse train at Rotor/60 × Pockets: narrow-band harmonics, a turbulent rasp gated by
                    // every pocket, and a rotor-rate modulation (no two pockets are quite alike).
                    const Base = Rotor / 60 * M.Pockets;
                    this.Pocket += Base / Rate; this.Pocket -= Math.floor(this.Pocket);
                    let Whine = 0;
                    M.Harmonics.forEach(([Multiple, Level], Slot) => { const Hz = Base * Multiple; Whine += this.Tones[Slot].Run(Hz) * Level * Fade(Hz); });
                    const Rotation = 1 + 0.14 * Math.sin(Tau * this.RotorPhase) + 0.06 * Math.sin(Tau * 2 * this.RotorPhase + 0.7);
                    const Gate = Math.pow(1 - this.Pocket, 6);                    // the backflow burst at each pocket opening
                    const Chug = 1 - M.Pulsing + M.Pulsing * 2 * Math.pow(Math.max(0, Math.sin(Tau * this.Pocket)), 4);
                    if (Tune) this.Rasp.Shape('Band', Clamp(Base * 3.2, 600, 12000), 1.2);
                    const Rasp = this.Rasp.Run(Noise.White()) * Gate * 2.4;
                    Mid += (Whine * Chug + Rasp) * Rotation * M.Whine * 1.25 * Load * Speed;
                    if (M.TimingGear) { const Hz = Rotor / 60 * M.TimingGear; Mid += this.Tones[6].Run(Hz) * Fade(Hz) * M.Whine * 0.35 * Speed * (0.4 + 0.6 * Throttle); }
                    // Roots gear backlash rattle at idle (light load lets the timing gears chatter).
                    if (M.Rattle && Rpm < 1800)
                    {
                        this.RattlePhase += Rotor / 60 * 2 / Rate;
                        if (this.RattlePhase >= 1) { this.RattlePhase -= 1; if (Noise.Next() < 0.7) this.Rattle = 1; }
                        if (Tune) this.Bands[3].Shape('Band', 2400, 3);
                        Mid += this.Bands[3].Run(Noise.White() * this.Rattle) * M.Rattle * (1 - Throttle) * (1 - Smoothstep(1200, 1800, Rpm)) * 4;
                        this.Rattle *= 0.993;
                    }
                }
                else
                {
                    // Centrifugal: impeller blade-pass (a breathy band, modulated by the impeller shaft) and the step-up
                    // gears' zing, both climbing with rpm², over the air the impeller moves.
                    const Impeller = Rotor * M.StepUp, Blade = Impeller / 60 * M.Blades, Mesh = Rotor / 60 * M.GearTeeth;
                    const Level = M.Whine * 1.25 * Math.pow(Clamp(Rpm / Top, 0, 1.1), 1.5) * (0.3 + 0.7 * Throttle);
                    const Spin = 1 + 0.15 * Math.sin(Tau * this.RotorPhase * M.StepUp);
                    Mid += (this.Tones[0].Run(Mesh) * Fade(Mesh) + this.Tones[2].Run(Mesh * 2) * 0.3 * Fade(Mesh * 2)) * Level;
                    Mid += this.Tones[4].Run(Blade) * Fade(Blade) * Level * 0.6 * Spin;
                    if (Tune) this.Bands[4].Shape('Band', Clamp(Blade * 0.4, 800, 9000), 0.9);
                    Mid += this.Bands[4].Run(this.Intake.Run()) * Boost * M.Whine * 1.2;
                }
                // Intake roar: the blower inhales hard at full throttle.
                if (Tune) this.IntakeBand.Shape('Band', Mix(380, 1300, Clamp(Rpm / Top, 0, 1)), 0.9);
                Mid += this.IntakeBand.Run(this.Intake.Run()) * M.Intake * Throttle * Speed * 0.6;
            }

            // Lift detection: blow-off vent or compressor surge.
            if (Held - Throttle > 0.35 && Boost > 0.3 && !this.Valve.Busy && !this.Stall.Busy)
            {
                if (M.Valve === 'Atmos' || M.Valve === 'Recirc') this.Valve.Fire(Boost);
                if (M.Flutter) this.Stall.Fire(Boost);
            }
            if (M.Vent) { const [VentMid, VentSide] = this.Valve.Run(); Mid += VentMid * M.Vent * 0.45; Side += VentSide * M.Vent * 0.45; }
            if (M.Flutter) Mid += this.Stall.Run() * M.Flutter * 0.95;

            let Left0 = Mid + Side, Right0 = Mid - Side;
            if (this.Pickup === 'Cabin') { const Muffled = this.Cabin.Run(Mid) * 1.2; Left0 = Muffled + Side * 0.5; Right0 = Muffled - Side * 0.5; }
            else if (this.Pickup === 'Dry') Left0 = Right0 = Mid;
            Left[Index] += SoftClip(this.Blocks[0].Run(Left0), 0.8);
            Right[Index] += SoftClip(this.Blocks[1].Run(Right0), 0.8);
        }
    }
}
