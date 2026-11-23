// IMPACT FOLEY
// 🧩 One-shot destruction and energy cues built from physics-shaped layers: explosion (blast pressure wave, fireball
//    roar, debris and echoes off distant surfaces), shockwave (an N-wave double crack, air rush and rattling), EMP pulse
//    (charge whine, discharge drop, arcing and electronics dying), sonic blast (a beating pressure beam), window shatter
//    (tempered glass crazing into cubes), glass break (a pane breaking into ringing shards) and metal crash (impact,
//    sheet buckling, scrape, debris and settling creaks).
// 📝 The layer helpers (Overpressure, Thump, Roar, Scatter, Bounce, Whine, Creak, Arcing, Echoes, Distant, Settle) are
//    exported for Ballistics.js. Same contract as CabinFoley: each cue in Cues has a Group, a Label, a default length and Draw(Left, Right, Rate,
//    Options); RenderCue() returns the buffers normalized to the cue's Peak. Options.Distance (metres) moves the
//    explosion, shockwave and crash away: later, darker, with more echo. Nothing is sampled.

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, OnePole, Biquad, Resonator, DcBlock, DelayLine, PanGains, SoftClip, Strike } from './SignalKit.js';
import { Detonate, Place } from './Detonation.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    PIECES
//------------------------------------------------------------------------------------------------------------------------

const At = (Rate, Time) => Math.max(0, Math.round(Time * Rate));

/**
 * Blast overpressure (Friedlander form): an instant rise, a positive phase that decays through zero, then a longer, weaker
 * negative (suction) phase. Played at audio rate it is the chest-thump of a blast; Lasts is the positive phase length.
 */
export function Overpressure(Left, Right, Rate, Time, { Lasts = 0.012, Gain = 1, Pan = 0, Low = 4000 } = {})
{
    const Start = At(Rate, Time), Count = Math.min(Left.length - Start, Math.round(Lasts * 8 * Rate));
    const Soften = new Biquad(Rate).Shape('Low', Low, 0.6), [GainL, GainR] = PanGains(Pan);
    for (let Index = 0; Index < Count; Index++)
    {
        const T = Index / Rate / Lasts;
        const Wave = (1 - T) * Math.exp(-1.6 * T);        // goes negative after T = 1 and creeps back to zero
        const Out = Soften.Run(Wave) * Gain;
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/** Low sine thump whose pitch falls from Hz to EndHz while it decays; soft-clipped so it hits like a sub drop. */
export function Thump(Left, Right, Rate, Time, { Hz = 60, EndHz = 28, Decay = 1.2, Gain = 0.6, Pan = 0, Drive = 1.6, Attack = 0.004 } = {})
{
    const Start = At(Rate, Time), Count = Math.min(Left.length - Start, Math.round(Decay * Rate));
    const [GainL, GainR] = PanGains(Pan);
    let Phase = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const Age = Index / Rate, Progress = Index / Count;
        Phase += (EndHz + (Hz - EndHz) * Math.exp(-Age * 6 / Decay)) / Rate;
        const Envelope = Math.min(1, Age / Attack) * Math.pow(1 - Progress, 2.2);
        const Out = SoftClip(Math.sin(Tau * Phase) * Envelope * Drive, 1) / Drive * Gain;
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/**
 * Filtered noise body (fireball roar, air rush, scrape): a low-pass whose cut-off slides From → To, a band floor, an
 * attack and a curved decay; Shape(Progress) can replace the envelope. Stereo width comes from decorrelated noise.
 */
export function Roar(Left, Right, Rate, Time, Seconds, { From = 6000, To = 300, Floor = 30, Gain = 0.3, Attack = 0.01, Curve = 1.5, Width = 0.6, Seed = 9, Shape = null, Grit = 0 } = {})
{
    const Start = At(Rate, Time), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    const Noise = new SeededNoise(Seed), Side = new SeededNoise(Seed + 101);
    const Lows = [new Biquad(Rate), new Biquad(Rate)], Floors = [new Biquad(Rate).Shape('High', Floor, 0.7), new Biquad(Rate).Shape('High', Floor, 0.7)];
    const Crackle = new OnePole(Rate, 40);
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Index / Count, Age = Index / Rate;
        if ((Index & 31) === 0) { const Hz = To * Math.pow(From / To, Math.pow(1 - Progress, 1.7)); Lows[0].Shape('Low', Hz, 0.7); Lows[1].Shape('Low', Hz * 1.04, 0.7); }
        const Envelope = Shape ? Shape(Progress) : Math.min(1, Age / Attack) * Math.pow(1 - Progress, Curve);
        // Grit: random amplitude bursts (turbulence, crackling flame) instead of smooth noise.
        const Rough = Grit ? Mix(1, Clamp(Crackle.Run(Noise.White() * 6) * 3 + 0.6, 0, 2.5), Grit) : 1;
        const Middle = Noise.White(), Edge = Side.White();
        const InL = Middle + Edge * Width, InR = Middle - Edge * Width;
        Left[Start + Index] += Floors[0].Run(Lows[0].Run(InL)) * Envelope * Gain * Rough;
        Right[Start + Index] += Floors[1].Run(Lows[1].Run(InR)) * Envelope * Gain * Rough;
    }
}

/**
 * Scatter of small struck pieces between Time and Time + Seconds with a density that falls away (Count total). Each
 * piece calls Make(Noise, Progress) → Strike options, so the caller picks the material (glass, metal, stone, plastic).
 */
export function Scatter(Left, Right, Rate, Time, Seconds, Count, Make, { Seed = 21, Bunch = 2.2 } = {})
{
    const Noise = new SeededNoise(Seed);
    for (let Piece = 0; Piece < Count; Piece++)
    {
        const Progress = Math.pow(Noise.Next(), Bunch);          // most land early, a few straggle in late
        Strike(Left, Right, Rate, Time + Progress * Seconds, { Seed: Seed + Piece * 7, ...Make(Noise, Progress) });
    }
}

/** Bouncing object: hits closer and closer together, each softer (a bolt, a hubcap, a chunk of glass). */
export function Bounce(Left, Right, Rate, Time, { Modes, Hits = 6, First = 0.32, Ratio = 0.62, Gain = 0.5, Pan = 0, Seed = 31 } = {})
{
    let When = Time, Gap = First, Level = Gain;
    for (let Hit = 0; Hit < Hits; Hit++)
    {
        Strike(Left, Right, Rate, When, { Modes, Burst: 0.0004, Gain: Level, Pan, Seed: Seed + Hit });
        When += Gap; Gap *= Ratio; Level *= 0.68;
    }
}

/** Pitched whine: harmonic tone sliding From → To Hz with vibrato and an envelope (capacitor charge, electronics). */
export function Whine(Left, Right, Rate, Time, Seconds, { From = 400, To = 4000, Gain = 0.1, Curve = 2, Harmonics = [1, 0.4, 0.2], Shape = null, Wobble = 0.004, Pan = 0 } = {})
{
    const Start = At(Rate, Time), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    const [GainL, GainR] = PanGains(Pan);
    let Phase = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Index / Count, Age = Index / Rate;
        const Hz = From * Math.pow(To / From, Math.pow(Progress, Curve)) * (1 + Wobble * Math.sin(Tau * 6.5 * Age));
        Phase += Hz / Rate;
        let Tone = 0;
        for (let Order = 0; Order < Harmonics.length; Order++) if (Hz * (Order + 1) < Rate * 0.45) Tone += Math.sin(Tau * Phase * (Order + 1)) * Harmonics[Order];
        const Envelope = Shape ? Shape(Progress) : Smoothstep(0, 0.1, Progress) * (1 - Smoothstep(0.9, 1, Progress));
        const Out = Tone * Envelope * Gain;
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/** Stick-slip creak: a train of slips (pitch sliding From → To with jitter) each ringing a few metal modes. */
export function Creak(Left, Right, Rate, Time, Seconds, { From = 300, To = 200, Gain = 0.2, Pan = 0, Seed = 41, Modes = [[820, 0.012], [1900, 0.008], [3600, 0.005]] } = {})
{
    const Start = At(Rate, Time), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    const Noise = new SeededNoise(Seed), Rings = Modes.map(([Hz, Decay]) => new Resonator(Rate, Hz * Noise.Range(0.9, 1.1), Decay)), [GainL, GainR] = PanGains(Pan);
    let Phase = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Index / Count;
        Phase += Mix(From, To, Progress) * (1 + Noise.Gauss() * 0.12) / Rate;
        const Slip = Phase >= 1 ? (Phase -= 1, Noise.Range(0.3, 1)) : 0;
        let Out = 0;
        for (const Ring of Rings) Out += Ring.Run(Slip) / Ring.Norm * 0.02;
        Out *= Gain * Math.sin(Math.PI * Progress);
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/** Electric arcing: clusters of very short broadband snaps with a buzzing 100–120 Hz ionization hum between them. */
export function Arcing(Left, Right, Rate, Time, Seconds, { Gain = 0.3, Density = 90, Seed = 61, Pan = 0 } = {})
{
    const Start = At(Rate, Time), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    const Noise = new SeededNoise(Seed), Bright = new Biquad(Rate).Shape('High', 1800, 0.7), Hum = new Biquad(Rate).Shape('Band', 120, 4);
    let Snap = 0, Burst = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Index / Count;
        const Rate_ = Density * (1 - Progress * 0.7);
        if (Noise.Next() < Rate_ / Rate) { Snap = Noise.Range(0.4, 1); Burst = Math.round(Noise.Range(0.0004, 0.004) * Rate); }
        let Out = 0;
        if (Burst > 0) { Out += Bright.Run(Noise.White()) * Snap; Burst--; } else Bright.Run(0);
        Out += Hum.Run(Noise.White() * (Snap > 0.05 ? 1 : 0)) * 0.6;
        Snap *= 0.9995;
        Out *= Gain * (1 - Progress * 0.5);
        const Spread = Noise.Range(-0.6, 0.6) + Pan, [GainL, GainR] = PanGains(Spread);
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/**
 * Outdoor echoes: copies of the whole buffer returning from distant surfaces (buildings, a hillside, a tree line),
 * each later, quieter and darker, alternating sides, plus a diffuse low-passed feedback wash.
 */
export function Echoes(Left, Right, Rate, { Taps = [[0.21, 0.32, -0.6], [0.47, 0.22, 0.7], [0.83, 0.15, -0.2], [1.38, 0.09, 0.4]], Dark = 1800, Wash = 0.35 } = {})
{
    const Dry = [Float32Array.from(Left), Float32Array.from(Right)];
    for (const [Delay, Level, Pan] of Taps)
    {
        const Offset = Math.round(Delay * Rate), [GainL, GainR] = PanGains(Pan);
        const Shade = new Biquad(Rate).Shape('Low', Dark * (1 - Delay * 0.3), 0.6);
        for (let Index = Offset; Index < Left.length; Index++)
        {
            const Back = Shade.Run((Dry[0][Index - Offset] + Dry[1][Index - Offset]) * 0.5) * Level;
            Left[Index] += Back * GainL; Right[Index] += Back * GainR;
        }
    }
    if (Wash > 0)
    {
        // Two cross-fed comb lines for a late rumbling tail.
        const Lines = [new DelayLine(Rate), new DelayLine(Rate)], Lengths = [0.137 * Rate, 0.191 * Rate], Damp = [new OnePole(Rate, Dark * 0.6), new OnePole(Rate, Dark * 0.6)];
        for (let Index = 0; Index < Left.length; Index++)
        {
            const BackL = Damp[0].Run(Lines[0].Read(Lengths[0])), BackR = Damp[1].Run(Lines[1].Read(Lengths[1]));
            Lines[0].Write(Dry[0][Index] * 0.4 + BackR * 0.72);
            Lines[1].Write(Dry[1][Index] * 0.4 + BackL * 0.72);
            Left[Index] += BackL * Wash; Right[Index] += BackR * Wash;
        }
    }
}

/** Air absorption over a distance: high frequencies die first (a far blast is a dull thud, a near one a crack). */
export function Distant(Left, Right, Rate, Metres)
{
    if (Metres <= 20) return;
    const Hz = Clamp(14000 * Math.pow(20 / Metres, 0.8), 700, 14000);
    const Shades = [new Biquad(Rate).Shape('Low', Hz, 0.6), new Biquad(Rate).Shape('Low', Hz, 0.6)];
    for (let Index = 0; Index < Left.length; Index++) { Left[Index] = Shades[0].Run(Left[Index]); Right[Index] = Shades[1].Run(Right[Index]); }
}

/** Removes DC and sub-sonic drift left by the pressure waves (keeps speakers and meters honest). */
export function Settle(Left, Right, Rate)
{
    const Blocks = [new DcBlock(Rate, 12), new DcBlock(Rate, 12)];
    for (let Index = 0; Index < Left.length; Index++) { Left[Index] = Blocks[0].Run(Left[Index]); Right[Index] = Blocks[1].Run(Right[Index]); }
}

// Mode sets: [Hz, decay seconds, gain].
const Glass = (Noise, Scale = 1) => [[Noise.Range(2400, 4200) * Scale, Noise.Range(0.04, 0.12), 1], [Noise.Range(5200, 7800) * Scale, Noise.Range(0.03, 0.08), 0.7], [Noise.Range(8500, 12500) * Scale, Noise.Range(0.015, 0.05), 0.5]];
const Cube = (Noise) => [[Noise.Range(4200, 7500), Noise.Range(0.006, 0.02), 1], [Noise.Range(8500, 13000), Noise.Range(0.004, 0.012), 0.6]];
const Panel = [[46, 0.5, 1.4], [73, 0.4, 1.1], [118, 0.32, 0.9], [187, 0.25, 0.7], [290, 0.18, 0.5], [455, 0.12, 0.35], [720, 0.08, 0.25], [1150, 0.05, 0.15]];
const Shard = (Noise, Low = 900, High = 4200) => [[Noise.Range(Low, High), Noise.Range(0.03, 0.12), 1], [Noise.Range(High, High * 2.3), Noise.Range(0.02, 0.06), 0.6], [Noise.Range(Low * 0.3, Low), Noise.Range(0.04, 0.1), 0.5]];
const Rock = (Noise) => [[Noise.Range(300, 1400), Noise.Range(0.008, 0.03), 1], [Noise.Range(1600, 4000), Noise.Range(0.004, 0.015), 0.5]];

//------------------------------------------------------------------------------------------------------------------------
//                                                    CUES
//------------------------------------------------------------------------------------------------------------------------

export const Cues = {
    // ── Energy ──────────────────────────────────────────────────────────────────────────────────────────────────────
    Explosion: {
        Group: 'Energy', Label: 'Explosion', Seconds: 6.5, Peak: 0.94,
        Draw(Left, Right, Rate, { Distance = 60 } = {})
        {
            // Sound arrives after Distance / 343 s; the crack thins out with range while the boom carries.
            const Arrive = 0.05 + Math.min(0.6, Distance / 343 * 0.15), Near = Clamp(1 - (Distance - 20) / 400, 0.15, 1);
            // Follow-up 20: a 4 kg surface burst with a big fireball (a vehicle going up): Detonation.js scales the blast,
            // the turbulent rumble and the fireball roar by the charge instead of a pitched sine thump.
            Detonate(Left, Right, Rate, Arrive, { Charge: 4, Ground: 'Open', Fire: 0.9, Seed: 11, Metres: Distance });
            // Debris: stones and metal fragments falling back over the next three seconds.
            Scatter(Left, Right, Rate, Arrive + 0.5, 3.2, Math.round(70 * Near + 10), (Noise, Progress) => ({
                Modes: Noise.Next() < 0.7 ? Rock(Noise) : Shard(Noise, 700, 3000), Burst: Noise.Range(0.0003, 0.002),
                Gain: Noise.Range(0.2, 1) * 0.9 * (1 - Progress * 0.6), Pan: Noise.Range(-0.9, 0.9),
            }), { Seed: 13, Bunch: 1.4 });
            Bounce(Left, Right, Rate, Arrive + 1.4, { Modes: [[1650, 0.08, 1], [4100, 0.05, 0.6], [690, 0.06, 0.5]], Hits: 5, First: 0.25, Gain: 0.6, Pan: 0.55, Seed: 14 });
            Place(Left, Right, Rate, 'Urban', { Wet: 1.1, Seed: 2 });
            Distant(Left, Right, Rate, Distance);
            Settle(Left, Right, Rate);
        },
    },
    Shockwave: {
        Group: 'Energy', Label: 'Shockwave', Seconds: 4.0, Peak: 0.9,
        Draw(Left, Right, Rate, { Distance = 120 } = {})
        {
            // N-wave: a sudden compression, a linear fall to suction, a sudden return (the double crack of a boom).
            const Gap = 0.11 + Distance * 0.0002, Start = 0.08;
            const N = At(Rate, Start), Length = At(Rate, Gap), Soften = new Biquad(Rate).Shape('Low', 7000, 0.6);
            for (let Index = 0; Index < Length + Rate * 0.01 && N + Index < Left.length; Index++)
            {
                const Wave = Index < Length ? 1 - 2 * Index / Length : 0;
                const Out = Soften.Run(Wave) * 0.9;
                Left[N + Index] += Out; Right[N + Index] += Out;
            }
            Thump(Left, Right, Rate, Start, { Hz: 55, EndHz: 22, Decay: 1.6, Gain: 0.8, Drive: 2 });
            // The air behind the front: a rush that sweeps past left to right, then a low tumbling wind.
            Roar(Left, Right, Rate, Start + 0.02, 2.4, { From: 5500, To: 220, Floor: 60, Gain: 0.55, Attack: 0.02, Curve: 1.7, Width: 1, Seed: 15,
                Shape: (P) => Smoothstep(0, 0.03, P) * Math.pow(1 - P, 1.4) * (1 + 0.6 * Math.sin(Tau * 2.2 * P)) });
            Roar(Left, Right, Rate, Start + 0.3, 3.4, { From: 500, To: 80, Floor: 25, Gain: 0.35, Attack: 0.4, Curve: 1, Width: 1, Seed: 16, Grit: 0.4 });
            // Everything nearby shaken by it: window glass buzzing, a sign rattling, car panels drumming.
            Scatter(Left, Right, Rate, Start + 0.04, 0.9, 60, (Noise) => ({
                Modes: Noise.Next() < 0.5 ? Cube(Noise) : [[Noise.Range(180, 420), 0.05, 1], [Noise.Range(900, 1800), 0.03, 0.5]], Burst: 0.0003,
                Gain: Noise.Range(0.1, 0.4), Pan: Noise.Range(-1, 1),
            }), { Seed: 17, Bunch: 2.5 });
            Strike(Left, Right, Rate, Start + 0.03, { Modes: Panel, Burst: 0.004, Gain: 0.9, Seed: 18 });
            Echoes(Left, Right, Rate, { Taps: [[0.36, 0.3, 0.7], [0.82, 0.18, -0.6], [1.5, 0.1, 0.2]], Dark: 1600, Wash: 0.3 });
            Distant(Left, Right, Rate, Distance);
            Settle(Left, Right, Rate);
        },
    },
    EmpPulse: {
        Group: 'Energy', Label: 'EMP pulse', Seconds: 6.0, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            // Charge: capacitor bank whine climbing, a rising hum under it, faint arcing as it nears full.
            Whine(Left, Right, Rate, 0.05, 1.7, { From: 180, To: 5200, Gain: 0.16, Curve: 1.8, Harmonics: [1, 0.5, 0.25, 0.12], Shape: (P) => Smoothstep(0, 0.6, P) * (1 - Smoothstep(0.98, 1, P)), Wobble: 0.006 });
            Whine(Left, Right, Rate, 0.05, 1.7, { From: 50, To: 120, Gain: 0.22, Curve: 1, Harmonics: [1, 0.6, 0.35, 0.2, 0.1], Shape: (P) => Smoothstep(0, 1, P) });
            Arcing(Left, Right, Rate, 1.2, 0.55, { Gain: 0.18, Density: 40, Seed: 62 });
            // Discharge: a pressure slap, a sub drop and a downward electrical sweep, crackling as it collapses.
            const Fire = 1.78;
            Overpressure(Left, Right, Rate, Fire, { Lasts: 0.006, Gain: 1.3, Low: 6000 });
            Thump(Left, Right, Rate, Fire, { Hz: 160, EndHz: 26, Decay: 2.4, Gain: 1.0, Drive: 2.5 });
            Whine(Left, Right, Rate, Fire, 1.3, { From: 2400, To: 60, Gain: 0.22, Curve: 0.45, Harmonics: [1, 0.7, 0.45, 0.3, 0.2, 0.12], Shape: (P) => Math.pow(1 - P, 1.5) * Smoothstep(0, 0.01, P), Wobble: 0.03 });
            Roar(Left, Right, Rate, Fire, 1.8, { From: 12000, To: 400, Floor: 200, Gain: 0.45, Attack: 0.002, Curve: 3, Grit: 0.8, Seed: 63 });
            Arcing(Left, Right, Rate, Fire, 1.6, { Gain: 0.4, Density: 160, Seed: 64 });
            // Electronics dying: a fan or inverter winding down, a relay drop-out, then ringing ears.
            Whine(Left, Right, Rate, Fire + 0.35, 2.6, { From: 1500, To: 90, Gain: 0.07, Curve: 0.7, Harmonics: [1, 0.3, 0.2], Shape: (P) => Math.pow(1 - P, 1.2), Pan: 0.4 });
            Strike(Left, Right, Rate, Fire + 0.42, { Modes: [[1750, 0.035, 0.8], [3150, 0.025, 0.6], [240, 0.03, 0.6]], Burst: 0.0004, Gain: 0.5, Pan: -0.5, Seed: 65 });
            Whine(Left, Right, Rate, Fire + 0.05, 3.9, { From: 3600, To: 3540, Gain: 0.035, Curve: 1, Harmonics: [1], Shape: (P) => Smoothstep(0, 0.08, P) * Math.pow(1 - P, 1.8), Wobble: 0.0008 });
            Echoes(Left, Right, Rate, { Taps: [[0.19, 0.22, -0.6], [0.43, 0.14, 0.6]], Dark: 3000, Wash: 0.25 });
            Settle(Left, Right, Rate);
        },
    },
    SonicBlast: {
        Group: 'Energy', Label: 'Sonic blast', Seconds: 4.5, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            // A focused acoustic weapon: a short spin-up, then a beam of two close tones beating hard (the pulsing
            // pressure), an infrasonic body under it and air turbulence, cutting off into a rushing release.
            Whine(Left, Right, Rate, 0.05, 0.6, { From: 90, To: 420, Gain: 0.18, Curve: 1.4, Harmonics: [1, 0.4, 0.3, 0.15], Shape: (P) => Smoothstep(0, 1, P) });
            const Beam = 0.62, Lasts = 2.0, Start = At(Rate, Beam), Count = Math.min(Left.length - Start, Math.round(Lasts * Rate));
            const Noise = new SeededNoise(71), Air = new Biquad(Rate).Shape('Band', 1400, 0.7);
            let A = 0, B = 0, C = 0;
            for (let Index = 0; Index < Count; Index++)
            {
                const Progress = Index / Count, Age = Index / Rate;
                const Envelope = Smoothstep(0, 0.02, Age) * (1 - Smoothstep(0.93, 1, Progress));
                const Beat = 11 + 7 * Progress;                                  // the throb speeds up as it holds
                A += 420 / Rate; B += (420 + Beat) / Rate; C += 38 / Rate;
                const Tone = Math.sin(Tau * A) + Math.sin(Tau * B) + 0.35 * Math.sin(Tau * A * 3) + 0.35 * Math.sin(Tau * B * 3);
                const Throb = 0.5 + 0.5 * Math.sin(Tau * (A - B));
                const Out = SoftClip(Tone * 0.6, 2) * 0.45 * Envelope + Math.sin(Tau * C) * 0.5 * Envelope * Throb + Air.Run(Noise.White()) * 0.25 * Envelope * Throb;
                Left[Start + Index] += Out; Right[Start + Index] += Out * 0.94;
            }
            Overpressure(Left, Right, Rate, Beam, { Lasts: 0.01, Gain: 1, Low: 3000 });
            Roar(Left, Right, Rate, Beam + Lasts - 0.12, 1.6, { From: 4000, To: 150, Floor: 40, Gain: 0.5, Attack: 0.01, Curve: 1.8, Width: 1, Seed: 72 });
            Thump(Left, Right, Rate, Beam + Lasts - 0.1, { Hz: 80, EndHz: 30, Decay: 1.2, Gain: 0.6, Drive: 1.8 });
            Echoes(Left, Right, Rate, { Taps: [[0.25, 0.25, 0.6], [0.55, 0.15, -0.6]], Dark: 2500, Wash: 0.2 });
            Settle(Left, Right, Rate);
        },
    },

    // ── Glass ───────────────────────────────────────────────────────────────────────────────────────────────────────
    WindowShatter: {
        Group: 'Glass', Label: 'Side window shatter', Seconds: 2.4, Peak: 0.85,
        Draw(Left, Right, Rate)
        {
            // Tempered side glass: a hard tap, the sharp pop as the stressed sheet lets go, the crazing hiss as cracks
            // race across it, then thousands of small cubes pouring out and tinkling on the sill and the road.
            Strike(Left, Right, Rate, 0.05, { Modes: [[2900, 0.02, 1], [5600, 0.012, 0.6], [380, 0.03, 0.5]], Burst: 0.0004, Gain: 1.2, Pan: -0.4, Seed: 81 });
            Overpressure(Left, Right, Rate, 0.09, { Lasts: 0.0015, Gain: 0.9, Low: 12000, Pan: -0.4 });
            Roar(Left, Right, Rate, 0.09, 0.22, { From: 16000, To: 5000, Floor: 3000, Gain: 0.7, Attack: 0.001, Curve: 2.5, Grit: 0.7, Seed: 82 });
            Scatter(Left, Right, Rate, 0.09, 0.35, 260, (Noise) => ({ Modes: Cube(Noise), Burst: 0.0003, Gain: Noise.Range(0.5, 1.6), Pan: Noise.Range(-0.8, 0.2) }), { Seed: 83, Bunch: 2.0 });
            Scatter(Left, Right, Rate, 0.22, 1.9, 320, (Noise, Progress) => ({ Modes: Cube(Noise), Burst: 0.0003, Gain: Noise.Range(0.2, 1.2) * (1 - Progress * 0.6), Pan: Noise.Range(-1, 0.6) }), { Seed: 84, Bunch: 1.8 });
            Roar(Left, Right, Rate, 0.25, 1.6, { From: 9000, To: 3500, Floor: 2500, Gain: 0.14, Attack: 0.05, Curve: 2.2, Grit: 0.9, Seed: 85 });
            Strike(Left, Right, Rate, 0.09, { Modes: [[120, 0.08, 1], [260, 0.05, 0.6]], Burst: 0.003, Gain: 0.6, Pan: -0.4, Seed: 86 });
            Settle(Left, Right, Rate);
        },
    },
    GlassBreak: {
        Group: 'Glass', Label: 'Glass breaking', Seconds: 2.6, Peak: 0.85,
        Draw(Left, Right, Rate)
        {
            // Annealed pane (a shop window or a lamp lens): the impact crack, big ringing shards breaking away and
            // falling, the smaller ones scattering and bouncing, and a few late pieces settling.
            Strike(Left, Right, Rate, 0.05, { Modes: [[1800, 0.05, 1], [3700, 0.04, 0.8], [6900, 0.025, 0.5], [210, 0.05, 0.5]], Burst: 0.0006, Gain: 0.6, Seed: 91 });
            Overpressure(Left, Right, Rate, 0.052, { Lasts: 0.002, Gain: 0.3, Low: 10000 });
            Roar(Left, Right, Rate, 0.05, 0.15, { From: 14000, To: 4000, Floor: 1500, Gain: 0.22, Attack: 0.001, Curve: 2, Grit: 0.6, Seed: 92 });
            const Noise = new SeededNoise(93);
            for (let Piece = 0; Piece < 9; Piece++)
            {
                // Large shards: each rings as it breaks away, then hits the ground a moment later.
                const Breaks = 0.07 + Noise.Next() * 0.25, Lands = Breaks + Noise.Range(0.35, 0.7), Pan = Noise.Range(-0.8, 0.8);
                Strike(Left, Right, Rate, Breaks, { Modes: Glass(Noise, Noise.Range(0.5, 0.9)), Burst: 0.0012, Gain: Noise.Range(2, 4), Pan, Seed: 94 + Piece });
                Strike(Left, Right, Rate, Lands, { Modes: Glass(Noise, Noise.Range(0.7, 1.2)), Burst: 0.0012, Gain: Noise.Range(2.5, 4.5), Pan, Seed: 120 + Piece, Breath: { Low: 3000, High: 12000, Decay: 0.05, Gain: 0.05 } });
                Scatter(Left, Right, Rate, Lands, 0.5, 24, (N) => ({ Modes: Glass(N, N.Range(1.1, 1.6)), Burst: 0.0004, Gain: N.Range(0.4, 1.8), Pan: Pan + N.Range(-0.3, 0.3) }), { Seed: 140 + Piece * 3, Bunch: 1.6 });
            }
            Bounce(Left, Right, Rate, 1.25, { Modes: [[3300, 0.09, 1], [7200, 0.05, 0.6]], Hits: 5, First: 0.18, Gain: 2.2, Pan: 0.6, Seed: 170 });
            Bounce(Left, Right, Rate, 1.6, { Modes: [[4100, 0.07, 1], [8800, 0.04, 0.5]], Hits: 4, First: 0.14, Gain: 1.6, Pan: -0.5, Seed: 180 });
            Echoes(Left, Right, Rate, { Taps: [[0.031, 0.25, 0.5], [0.053, 0.18, -0.5], [0.089, 0.12, 0.2]], Dark: 6000, Wash: 0.1 });
            Settle(Left, Right, Rate);
        },
    },

    // ── Crash ───────────────────────────────────────────────────────────────────────────────────────────────────────
    MetalCrash: {
        Group: 'Crash', Label: 'Metal crumple / crash', Seconds: 6.0, Peak: 0.94,
        Draw(Left, Right, Rate, { Distance = 15 } = {})
        {
            // Impact: two cars' structures meet — a huge low body hit, the bumper beam and plastic cracking at once.
            const Hit = 0.08;
            Thump(Left, Right, Rate, Hit, { Hz: 85, EndHz: 38, Decay: 0.7, Gain: 0.9, Drive: 2.4, Attack: 0.002 });
            Strike(Left, Right, Rate, Hit, { Modes: Panel, Burst: 0.006, Gain: 2.4, Seed: 201, Breath: { Low: 300, High: 6000, Decay: 0.18, Gain: 0.9 } });
            Strike(Left, Right, Rate, Hit + 0.004, { Modes: [[620, 0.06, 1], [1420, 0.04, 0.7], [2900, 0.025, 0.5]], Burst: 0.003, Gain: 1.1, Pan: 0.2, Seed: 202 });
            // Crumple: the sheet buckles in a run of snaps (stick-slip folding), each fold pitched lower as the panel
            // gives, under a grinding tearing noise.
            const Noise = new SeededNoise(203);
            let When = Hit + 0.01;
            for (let Fold = 0; Fold < 34; Fold++)
            {
                const Progress = Fold / 34, Sink = Mix(1, 0.55, Progress);
                Strike(Left, Right, Rate, When, {
                    Modes: [[Noise.Range(140, 260) * Sink, Noise.Range(0.06, 0.14), 1], [Noise.Range(480, 900) * Sink, Noise.Range(0.04, 0.08), 0.8], [Noise.Range(1500, 2800) * Sink, Noise.Range(0.02, 0.05), 0.55], [Noise.Range(3800, 6500), 0.012, 0.3]],
                    Burst: Noise.Range(0.001, 0.004), Gain: Noise.Range(0.5, 1.1) * (1 - Progress * 0.6), Pan: Noise.Range(-0.4, 0.5), Seed: 210 + Fold,
                });
                When += Noise.Range(0.006, 0.03) * (1 + Progress * 2.5);
            }
            Roar(Left, Right, Rate, Hit, 0.9, { From: 5000, To: 700, Floor: 180, Gain: 0.45, Attack: 0.004, Curve: 1.4, Grit: 0.95, Seed: 204 });
            // Glass and plastic: the headlamp lens and grille bursting, the bumper cover splitting.
            Scatter(Left, Right, Rate, Hit + 0.01, 0.5, 120, (N) => ({ Modes: N.Next() < 0.6 ? Glass(N, N.Range(0.9, 1.4)) : [[N.Range(1800, 3200), 0.012, 1], [N.Range(3800, 6000), 0.007, 0.5]], Burst: 0.0003, Gain: N.Range(0.1, 0.5), Pan: N.Range(-0.7, 0.7) }), { Seed: 205, Bunch: 2 });
            // Slide: the locked wreck scraping along the tarmac for a second, then lurching to a stop.
            Roar(Left, Right, Rate, Hit + 0.35, 1.5, { From: 3600, To: 1200, Floor: 400, Gain: 0.22, Curve: 0.8, Grit: 0.85, Seed: 206, Shape: (P) => Smoothstep(0, 0.15, P) * (1 - Smoothstep(0.7, 1, P)) });
            Strike(Left, Right, Rate, Hit + 1.75, { Modes: Panel.map(([Hz, Decay, Gain]) => [Hz * 1.1, Decay * 0.7, Gain]), Burst: 0.004, Gain: 0.8, Seed: 207 });
            // Debris and aftermath: a hubcap rolling off and settling, bolts and trim tinkling, the metal ticking and
            // creaking as it relaxes, coolant hissing.
            Bounce(Left, Right, Rate, Hit + 0.9, { Modes: [[980, 0.15, 1], [2450, 0.1, 0.7], [4300, 0.06, 0.4]], Hits: 7, First: 0.34, Ratio: 0.72, Gain: 0.55, Pan: 0.7, Seed: 208 });
            Scatter(Left, Right, Rate, Hit + 0.3, 1.6, 26, (N) => ({ Modes: [[N.Range(2200, 5200), N.Range(0.04, 0.12), 1], [N.Range(6000, 9500), 0.03, 0.5]], Burst: 0.0002, Gain: N.Range(0.1, 0.4), Pan: N.Range(-0.9, 0.9) }), { Seed: 209, Bunch: 1.5 });
            let Settling = Hit + 2.0;
            for (let Groan = 0; Groan < 5; Groan++)
            {
                Settling += Noise.Range(0.25, 0.85);
                Creak(Left, Right, Rate, Settling, Noise.Range(0.15, 0.45), { From: Noise.Range(90, 260), To: Noise.Range(40, 140), Gain: Noise.Range(0.15, 0.35), Pan: Noise.Range(-0.5, 0.5), Seed: 240 + Groan });
                Strike(Left, Right, Rate, Settling + Noise.Range(0.2, 0.5), { Modes: [[Noise.Range(2500, 4500), 0.01, 1]], Burst: 0.0002, Gain: 0.4, Seed: 230 + Groan });
            }
            Roar(Left, Right, Rate, Hit + 1.9, 3.8, { From: 7000, To: 5000, Floor: 2500, Gain: 0.05, Attack: 0.5, Curve: 0.8, Seed: 211 });
            Echoes(Left, Right, Rate, { Taps: [[0.09, 0.2, -0.6], [0.23, 0.14, 0.6], [0.5, 0.08, 0]], Dark: 3200, Wash: 0.15 });
            Distant(Left, Right, Rate, Distance);
            Settle(Left, Right, Rate);
        },
    },
};

/** Renders one cue into fresh stereo buffers, normalized so its loudest sample sits at the cue's Peak. */
export function RenderCue(Name, Rate, Options = {})
{
    const Cue = Cues[Name];
    if (!Cue) throw new Error(`Unknown cue ${Name}`);
    const Seconds = Options.Seconds ?? Cue.Seconds;
    const Left = new Float32Array(Math.ceil(Seconds * Rate)), Right = new Float32Array(Left.length);
    Cue.Draw(Left, Right, Rate, { ...Options, Seconds });
    let Loudest = 0;
    for (let Index = 0; Index < Left.length; Index++) Loudest = Math.max(Loudest, Math.abs(Left[Index]), Math.abs(Right[Index]));
    const Scale = Loudest > 0 ? (Options.Peak ?? Cue.Peak ?? 0.9) / Loudest : 1;
    // A gentle fade over the last 80 ms so a cut-off tail never clicks.
    const Fade = Math.round(0.08 * Rate);
    for (let Index = 0; Index < Left.length; Index++)
    {
        const Tail = Math.min(1, (Left.length - 1 - Index) / Fade);
        Left[Index] *= Scale * Tail; Right[Index] *= Scale * Tail;
    }
    return { Left, Right, Seconds };
}
