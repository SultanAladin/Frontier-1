// ELECTRIC AVIATION
// 🧩 Electric aircraft for the sound library: the propeller voice (blade-pass tone and its harmonics, which grow richer
//    as the blade tips approach the speed of sound, broadband blade noise that swishes at the blade rate, blade-to-blade
//    mismatch, gust wander) plus the electric motor's whine and inverter tones, and a flight renderer that moves the
//    aircraft past a microphone with Doppler shift, distance, air absorption and a ground reflection.
// 📝 Three airframes in the style of real ones (figures typical, not manufacturer data): a two-seat electric trainer,
//    a single-seat electric speed-record racer and a six-rotor eVTOL air taxi (rotors run at slightly different speeds,
//    so their tones beat). Cues: trainer run-up, trainer take-off, racer fly-over, air-taxi lift-off and transition.

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, OnePole, Biquad, DcBlock, Slew, Wander, PanGains } from './SignalKit.js';

const Sound = 343;

//------------------------------------------------------------------------------------------------------------------------
//                                                    AIRFRAMES
//------------------------------------------------------------------------------------------------------------------------

/**
 * Blades and Diameter (m) set the blade-pass frequency and tip speed; MaxRpm is full power; Props is the rotor count
 * (each gets a small speed offset, Spread); PolePairs and Carrier voice the motor and inverter; Level balances.
 */
export const Airframes = {
    ElectricTrainer: { Label: 'Electric trainer', Note: 'Two-seat trainer style: 2-blade prop, ~58 kW motor', Blades: 2, Diameter: 1.75, MaxRpm: 2450, IdleRpm: 750, Props: 1, Spread: 0, PolePairs: 10, Carrier: 12000, Motor: 0.05, Level: 1 },
    ElectricRacer: { Label: 'Electric racer', Note: 'Speed-record style: 3-blade prop, ~400 kW from three motors on one shaft', Blades: 3, Diameter: 2.3, MaxRpm: 2400, IdleRpm: 800, Props: 1, Spread: 0, PolePairs: 12, Carrier: 10000, Motor: 0.04, Level: 1 },
    AirTaxi: { Label: 'eVTOL air taxi', Note: 'Six tilting 5-blade rotors, slow tips for a quiet hover', Blades: 5, Diameter: 2.9, MaxRpm: 1150, IdleRpm: 300, Props: 6, Spread: 0.022, PolePairs: 16, Carrier: 14000, Motor: 0.035, Level: 0.6 },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    VOICE
//------------------------------------------------------------------------------------------------------------------------

/**
 * Propeller (or rotor set) voice, rendered dry at the source (mono). Feed({ Rpm 0–MaxRpm, Thrust 0–1, Airspeed m/s }).
 * Per rotor: harmonics n × blade-pass with a roll-off that flattens as tip Mach rises (the buzz of a loud prop),
 * shaft-order sidebands from blade mismatch, vortex noise band-passed around the tip speed and modulated at the blade
 * rate (the swish), the motor's 6th and 12th electrical orders and the inverter carrier with ±2 fe sidebands.
 */
export class PropellerVoice
{
    constructor(Rate, Airframe, { Seed = 41 } = {})
    {
        this.Rate = Rate; this.Airframe = Airframe; this.Noise = new SeededNoise(Seed);
        this.Rpm = new Slew(Rate, 0.35, 0); this.Thrust = new Slew(Rate, 0.12, 0); this.Airspeed = 0;
        this.Rotors = Array.from({ length: Airframe.Props }, (_, Index) => ({
            Offset: 1 + (Index - (Airframe.Props - 1) / 2) * Airframe.Spread * (0.7 + 0.6 * ((Index * 7) % 5) / 4),
            Lag: new Slew(Rate, 0.25 + Index * 0.04, 0), Phase: Index * 0.137, Motor: Index * 0.29, Carrier: Index * 0.41,
            Mismatch: Array.from({ length: Airframe.Blades }, () => 1 + this.Noise.Gauss() * 0.04),
            Gust: new Wander(Rate, 1.3, Seed + Index), Band: new Biquad(Rate), Pan: Airframe.Props > 1 ? (Index / (Airframe.Props - 1)) * 2 - 1 : 0,
        }));
        this.Count = 0;
    }

    Feed({ Rpm = 0, Thrust = 0, Airspeed = 0 }) { this.Rpm.Target = Math.max(0, Rpm); this.Thrust.Target = Clamp(Thrust, 0, 1); this.Airspeed = Airspeed; }

    /** Adds mono source samples into Out. */
    Render(Out, Offset = 0, Count = Out.length - Offset)
    {
        const A = this.Airframe, Rate = this.Rate, Noise = this.Noise, Nyquist = Rate * 0.45;
        for (let Index = Offset; Index < Offset + Count; Index++)
        {
            const Base = this.Rpm.Run(), Thrust = this.Thrust.Run(), Tune = (this.Count++ & 127) === 0;
            let Sum = 0;
            for (const Rotor of this.Rotors)
            {
                Rotor.Lag.Target = Base * Rotor.Offset;
                const Rpm = Rotor.Lag.Run(), Shaft = Rpm / 60;
                if (Rpm < 1) continue;
                Rotor.Phase = (Rotor.Phase + Shaft / Rate) % 1;
                const Tip = Math.hypot(Math.PI * A.Diameter * Shaft, this.Airspeed), Mach = Tip / Sound;
                const Rolloff = Mix(1.25, 0.22, Smoothstep(0.3, 0.92, Mach));
                const Loud = Math.pow(Mach / 0.6, 2.4) * (0.35 + 0.65 * Thrust) * (1 + 0.25 * Rotor.Gust.Run());
                // Tonal: blade-pass harmonics plus shaft orders (blade mismatch), synthesized from the shaft phase.
                let Tonal = 0;
                const Top = Math.min(40, Math.floor(Nyquist / Math.max(Shaft, 1)));
                for (let Order = 1; Order <= Top; Order++)
                {
                    const Harmonic = Order / A.Blades, Whole = Order % A.Blades === 0;
                    const Level = Whole ? Math.exp(-(Harmonic - 1) * Rolloff) : 0.06 * Math.exp(-(Harmonic - 0.5) * Rolloff * 1.3);
                    if (Level < 0.003) continue;
                    Tonal += Math.sin(Tau * Order * Rotor.Phase + Order * 0.7) * Level * (1 - Smoothstep(Nyquist * 0.8, Nyquist, Order * Shaft));
                }
                // Broadband: vortex shedding around the tip speed, swishing once per blade.
                if (Tune) Rotor.Band.Shape('Band', Clamp(Tip * 6, 250, 6000), 0.8);
                const Blade = Math.floor(Rotor.Phase * A.Blades), Within = Rotor.Phase * A.Blades - Blade;
                const Swish = 0.55 + 0.9 * Math.pow(0.5 + 0.5 * Math.cos(Tau * Within), 2) * Rotor.Mismatch[Blade];
                const Broad = Rotor.Band.Run(Noise.White()) * Swish * Math.pow(Mach / 0.6, 2) * (0.4 + 0.6 * Thrust) * 0.9;
                // Motor and inverter.
                const Electrical = Shaft * A.PolePairs;
                Rotor.Motor = (Rotor.Motor + Electrical * 6 / Rate) % 1;
                const Whine = (Math.sin(Tau * Rotor.Motor) + 0.4 * Math.sin(Tau * 2 * Rotor.Motor)) * A.Motor * (0.2 + 0.8 * Thrust) * Smoothstep(50, 400, Rpm);
                const Side = A.Carrier - 2 * Electrical;
                Rotor.Carrier = (Rotor.Carrier + Side / Rate) % 1;
                const Inverter = Math.sin(Tau * Rotor.Carrier) * A.Motor * 0.25 * Thrust * (1 - Smoothstep(Nyquist * 0.8, Nyquist, Side));
                Sum += (Tonal * 0.35 * Loud + Broad + Whine + Inverter) * A.Level / Math.sqrt(A.Props);
            }
            Out[Index] += Sum;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLIGHT
//------------------------------------------------------------------------------------------------------------------------

/**
 * Flies an airframe past a microphone at the origin (1.5 m up). Script(Time) → { X, Y, Z (m; Y = sideways offset,
 * Z = height), Rpm, Thrust, Airspeed }. The source is rendered dry, then each output sample reads it at the emission
 * time (Doppler), with 1/r spreading, distance low-pass and a ground bounce off the image source.
 */
function Fly(Left, Right, Rate, Airframe, Script, { Seed = 41, Lead = 1.5, Gain = 1 } = {})
{
    const Count = Left.length + Math.ceil(Lead * Rate), Source = new Float32Array(Count), Voice = new PropellerVoice(Rate, Airframe, { Seed });
    const Block = 64;
    for (let Index = 0; Index < Count; Index += Block)
    {
        const State = Script(Index / Rate - Lead);
        Voice.Feed(State);
        Voice.Render(Source, Index, Math.min(Block, Count - Index));
    }
    const Read = (Time) => { const At = (Time + Lead) * Rate, Whole = Math.floor(At), Part = At - Whole; return (Source[Whole] ?? 0) * (1 - Part) + (Source[Whole + 1] ?? 0) * Part; };
    const Air = new OnePole(Rate, 15000), Bounce = new OnePole(Rate, 9000), Blocks = [new DcBlock(Rate, 18), new DcBlock(Rate, 18)];
    const Mic = 1.5;
    for (let Index = 0; Index < Left.length; Index++)
    {
        const Time = Index / Rate;
        let Sent = Time, Where = Script(Time), Distance = 1;
        for (let Guess = 0; Guess < 3; Guess++) { Where = Script(Sent); Distance = Math.max(1, Math.hypot(Where.X, Where.Y, Where.Z - Mic)); Sent = Time - Distance / Sound; }
        const Image = Math.max(1, Math.hypot(Where.X, Where.Y, Where.Z + Mic)), SentImage = Time - Image / Sound;
        if ((Index & 63) === 0) { Air.Set(Clamp(16000 * Math.pow(25 / Distance, 0.55), 1500, 16000)); Bounce.Set(Clamp(9000 * Math.pow(25 / Image, 0.55), 900, 9000)); }
        const Mono = Air.Run(Read(Sent)) * 10 / Distance + Bounce.Run(Read(SentImage)) * 10 / Image * 0.5;
        const [GainL, GainR] = PanGains(Clamp(Where.X / Math.max(15, Math.abs(Where.Y) + Where.Z), -0.9, 0.9));
        Left[Index] += Blocks[0].Run(Mono * GainL * Gain); Right[Index] += Blocks[1].Run(Mono * GainR * Gain);
    }
}

/** Speed and position along a straight run with constant acceleration, capped at a top speed. */
const Run = (Time, Start, Accel, Top) => { const Reach = Top / Accel; return Time <= Reach ? [Start + 0.5 * Accel * Time * Time, Accel * Time] : [Start + 0.5 * Accel * Reach * Reach + Top * (Time - Reach), Top]; };

//------------------------------------------------------------------------------------------------------------------------
//                                                    CUES
//------------------------------------------------------------------------------------------------------------------------

export const Cues = {
    TrainerRunUp: {
        Group: 'Aircraft', Label: 'Electric trainer run-up', Seconds: 10, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            // On the apron, 9 m off the nose quarter: spin up to idle, run-up to full power, back to idle, stop.
            const A = Airframes.ElectricTrainer;
            Fly(Left, Right, Rate, A, (Time) =>
            {
                const Rpm = Time < 0.6 ? 0 : Time < 2.5 ? A.IdleRpm : Time < 6.2 ? Mix(A.IdleRpm, A.MaxRpm, Smoothstep(2.5, 3.6, Time)) : Time < 8.2 ? A.IdleRpm : 0;
                return { X: 5, Y: 7.5, Z: 1.6, Rpm, Thrust: Rpm > A.IdleRpm + 100 ? 1 : 0.1, Airspeed: 0 };
            });
        },
    },
    TrainerTakeoff: {
        Group: 'Aircraft', Label: 'Electric trainer take-off', Seconds: 18, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            // Microphone 25 m beside the runway: full-power roll from 200 m away, rotation, climb-out over the field.
            const A = Airframes.ElectricTrainer;
            Fly(Left, Right, Rate, A, (Time) =>
            {
                const Roll = Math.max(0, Time - 1.2), [X, Speed] = Run(Roll, -200, 2.1, 33);
                const Climb = Math.max(0, Roll - 11.4) * 3.2;
                return { X, Y: 25, Z: 1.4 + Climb, Rpm: Time < 1.2 ? Mix(A.IdleRpm, A.MaxRpm, Smoothstep(0, 1.2, Time)) : A.MaxRpm, Thrust: 1, Airspeed: Speed };
            });
        },
    },
    RacerFlyover: {
        Group: 'Aircraft', Label: 'Electric racer fly-over', Seconds: 10, Peak: 0.92,
        Draw(Left, Right, Rate)
        {
            // Low pass at 120 m/s (430 km/h), 35 m up and 15 m to the side, full power: tips near Mach 0.9.
            const A = Airframes.ElectricRacer;
            Fly(Left, Right, Rate, A, (Time) => ({ X: 120 * (Time - 5), Y: 15, Z: 35, Rpm: A.MaxRpm, Thrust: 1, Airspeed: 120 }), { Lead: 2.5 });
        },
    },
    AirTaxiHover: {
        Group: 'Aircraft', Label: 'eVTOL lift-off and transition', Seconds: 18, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            // 40 m from the pad: six rotors spin up, lift-off, hover at 30 m (beating rotor tones), then tilt and fly away.
            const A = Airframes.AirTaxi;
            Fly(Left, Right, Rate, A, (Time) =>
            {
                const Rise = Smoothstep(3.5, 9, Time) * 30, Away = Math.max(0, Time - 12), [X, Speed] = Run(Away, 0, 3.2, 60);
                const Rpm = Time < 1 ? 0 : Time < 3.5 ? Mix(A.IdleRpm, A.MaxRpm * 0.88, Smoothstep(1, 3.5, Time)) : Time < 12 ? A.MaxRpm * (Time < 9 ? 0.95 : 0.88) : Mix(A.MaxRpm * 0.88, A.MaxRpm * 0.62, Smoothstep(12, 16, Time));
                return { X: 30 + X, Y: 28, Z: 1 + Rise + Away * 1.5, Rpm, Thrust: Time < 3.5 ? 0.3 : Time < 9 ? 1 : 0.75, Airspeed: Speed };
            }, { Gain: 1.4 });
        },
    },
};

export function RenderCue(Name, Rate, Options = {})
{
    const Cue = Cues[Name];
    if (!Cue) throw new Error(`Unknown cue ${Name}`);
    const Seconds = Options.Seconds ?? Cue.Seconds;
    const Left = new Float32Array(Math.ceil(Seconds * Rate)), Right = new Float32Array(Left.length);
    Cue.Draw(Left, Right, Rate, { ...Options, Seconds });
    let Loudest = 0;
    for (let Index = 0; Index < Left.length; Index++) Loudest = Math.max(Loudest, Math.abs(Left[Index]), Math.abs(Right[Index]));
    const Scale = Loudest > 0 ? (Options.Peak ?? Cue.Peak ?? 0.9) / Loudest : 1, Fade = Math.round(0.08 * Rate);
    for (let Index = 0; Index < Left.length; Index++) { const Tail = Math.min(1, (Left.length - 1 - Index) / Fade); Left[Index] *= Scale * Tail; Right[Index] *= Scale * Tail; }
    return { Left, Right, Seconds };
}
