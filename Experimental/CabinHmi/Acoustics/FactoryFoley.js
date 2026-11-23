//------------------------------------------------------------------------------------------------------------------------
//    FactoryFoley.js · Experimental/CabinHmi/Acoustics
//    Factory, creak, spark and electronics cues, all synthesized: machines are motor tones (rotation rate × gear teeth,
//    vanes, commutator segments), impacts ring physical modes, sparks are clouds of sub-millisecond snaps, electrics
//    are mains hum at twice the line frequency with buzz. A small factory-hall reverb (Freeverb-style combs and
//    diffusers) places every cue in a big room. Loop cues fold their tail over their start so they repeat seamlessly; loop
//    lengths hold whole cycles of every machine tone in them.
//    Cues: { Group, Label, Seconds, Loop?, Room?, Peak?, Draw(Left, Right, Rate, Options) }; RenderCue(Name, Rate).
//------------------------------------------------------------------------------------------------------------------------

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, Biquad, PanGains, Strike } from './SignalKit.js';
import { Creak, Arcing, Whine, Bounce, Thump } from './ImpactFoley.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    BUILDING BLOCKS
//------------------------------------------------------------------------------------------------------------------------

const At = (Rate, Time) => Math.max(0, Math.round(Time * Rate));
const Edges = (Progress, Seconds, Fade = 0.02) => Math.min(1, Progress * Seconds / Fade, (1 - Progress) * Seconds / Fade);

/** Adds Make(Age, Progress) (a mono sample) over a span, panned. */
function Span(Left, Right, Rate, Time, Seconds, Make, Pan = 0)
{
    const Start = At(Rate, Time), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate)), [GainL, GainR] = PanGains(Pan);
    for (let Index = 0; Index < Count; Index++)
    {
        const Out = Make(Index / Rate, Index / Count);
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/** Band-limited noise (pink or white), decorrelated per side; Shape(Progress, Age) is the envelope. */
function Air(Left, Right, Rate, Time, Seconds, { Low = 300, High = 3000, Gain = 0.1, Shape = null, Pink = true, Seed = 1, Pan = 0, Width = 0.5 } = {})
{
    const Start = At(Rate, Time), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    const Noise = [new SeededNoise(Seed), new SeededNoise(Seed + 1)];
    const Bands = [0, 1].map(() => [new Biquad(Rate).Shape('High', Low, 0.7), new Biquad(Rate).Shape('Low', High, 0.7)]);
    const [GainL, GainR] = PanGains(Pan);
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Index / Count, Level = Gain * (Shape ? Shape(Progress, Index / Rate) : Edges(Progress, Seconds));
        const Source = [0, 1].map((Side) => (Pink ? Noise[Side].Pink() : Noise[Side].White()));
        const Shared = (Source[0] + Source[1]) * 0.5;
        const L = Bands[0][1].Run(Bands[0][0].Run(Mix(Shared, Source[0], Width))), R = Bands[1][1].Run(Bands[1][0].Run(Mix(Shared, Source[1], Width)));
        Left[Start + Index] += L * Level * GainL * 1.4; Right[Start + Index] += R * Level * GainR * 1.4;
    }
}

/**
 * Rotating machine: Hz(Age, Progress) is the shaft rate; Tones lists [multiple of the shaft rate, level] (gear teeth,
 * vanes, commutator segments, slots). Rough adds a little amplitude jitter so it is never a pure synth tone.
 */
function Motor(Left, Right, Rate, Time, Seconds, { Hz = () => 25, Tones = [[1, 1]], Gain = 0.1, Shape = null, Pan = 0, Rough = 0.15, Seed = 7 } = {})
{
    const Noise = new SeededNoise(Seed), Phases = Tones.map(() => Noise.Next());
    let Jitter = 0;
    Span(Left, Right, Rate, Time, Seconds, (Age, Progress) =>
    {
        const Shaft = Hz(Age, Progress);
        Jitter += (Noise.White() - Jitter) * 0.002;
        let Out = 0;
        for (let Slot = 0; Slot < Tones.length; Slot++)
        {
            const [Multiple, Level] = Tones[Slot], Pitch = Shaft * Multiple;
            Phases[Slot] = (Phases[Slot] + Pitch / Rate) % 1;
            if (Pitch < Rate * 0.45) Out += Math.sin(Tau * Phases[Slot]) * Level;
        }
        return Out * Gain * (1 + Rough * Jitter * 8) * (Shape ? Shape(Progress, Age) : Edges(Progress, Seconds));
    }, Pan);
}

/** Mains hum at Hz with harmonic levels; Buzz adds a clipped copy (loose laminations, a ballast, an arc). */
function Hum(Left, Right, Rate, Time, Seconds, { Hz = 100, Harmonics = [1, 0.5, 0.3, 0.15], Gain = 0.1, Buzz = 0, Shape = null, Pan = 0, Drift = 0 } = {})
{
    let Phase = 0;
    Span(Left, Right, Rate, Time, Seconds, (Age, Progress) =>
    {
        Phase = (Phase + Hz * (1 + Drift * Math.sin(Tau * 0.31 * Age)) / Rate) % 1;
        let Tone = 0;
        for (let Step = 0; Step < Harmonics.length; Step++) if (Hz * (Step + 1) < Rate * 0.45) Tone += Math.sin(Tau * Phase * (Step + 1)) * Harmonics[Step];
        if (Buzz) Tone += Clamp(Math.sin(Tau * Phase) * 5, -1, 1) * Math.abs(Math.sin(Tau * Phase * 2)) * Buzz;
        return Tone * Gain * (Shape ? Shape(Progress, Age) : Edges(Progress, Seconds));
    }, Pan);
}

/**
 * Crackle: random sub-millisecond snaps of bright noise. Sparks, a frying weld, an arcing contact. Density(Progress) in
 * snaps per second; most snaps are faint and a few are loud (power-law strength), each landing somewhere in the image.
 */
function Crackle(Left, Right, Rate, Time, Seconds, { Density = () => 300, Gain = 0.3, Low = 2500, Short = [0.00008, 0.0006], Spread = 0.7, Pan = 0, Seed = 1, Skew = 2.5 } = {})
{
    const Start = At(Rate, Time), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    const Noise = new SeededNoise(Seed), Bright = [new Biquad(Rate).Shape('High', Low, 0.7), new Biquad(Rate).Shape('High', Low, 0.7)];
    let Remain = 0, Strength = 0, GainL = 0, GainR = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        if (Remain <= 0 && Noise.Next() < Density(Index / Count) / Rate)
        {
            Remain = Math.max(2, Math.round(Noise.Range(Short[0], Short[1]) * Rate));
            Strength = Math.pow(Noise.Next(), Skew) * Gain;
            [GainL, GainR] = PanGains(Clamp(Pan + Noise.Range(-Spread, Spread), -1, 1));
        }
        const Snap = Remain > 0 ? (Remain--, Noise.White() * Strength) : 0;
        Left[Start + Index] += Bright[0].Run(Snap * GainL); Right[Start + Index] += Bright[1].Run(Snap * GainR);
    }
}

/**
 * Factory hall: Freeverb-style reverb (eight damped combs into four phase diffusers per side, the right side's delays
 * spread for width) after a short pre-delay. Wet is the reverb share; Size the comb feedback (room size); Damp the
 * high-frequency loss per bounce (concrete and steel keep it bright, so 0.2–0.35).
 */
export function Hall(Left, Right, Rate, { Wet = 0.3, Size = 0.86, Damp = 0.28, PreDelay = 0.018 } = {})
{
    const Scale = Rate / 44100, Spread = 23;
    const Build = (Offset) => ({
        Combs: [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617].map((Length) => ({ Line: new Float32Array(Math.round((Length + Offset) * Scale)), Cursor: 0, Kept: 0 })),
        Diffusers: [556, 441, 341, 225].map((Length) => ({ Line: new Float32Array(Math.round((Length + Offset) * Scale)), Cursor: 0 })),
    });
    const Sides = [Build(0), Build(Spread)], Outs = [Left, Right], Dry = [Left.slice(), Right.slice()], Delay = Math.round(PreDelay * Rate);
    for (let Index = 0; Index < Left.length; Index++)
    {
        const Feed = Index >= Delay ? (Dry[0][Index - Delay] + Dry[1][Index - Delay]) * 0.015 : 0;
        for (let Side = 0; Side < 2; Side++)
        {
            const Tank = Sides[Side];
            let Sum = 0;
            for (const Comb of Tank.Combs)
            {
                const Echo = Comb.Line[Comb.Cursor];
                Comb.Kept = Echo * (1 - Damp) + Comb.Kept * Damp;
                Comb.Line[Comb.Cursor] = Feed + Comb.Kept * Size;
                if (++Comb.Cursor >= Comb.Line.length) Comb.Cursor = 0;
                Sum += Echo;
            }
            for (const Diffuser of Tank.Diffusers)
            {
                const Held = Diffuser.Line[Diffuser.Cursor];
                Diffuser.Line[Diffuser.Cursor] = Sum + Held * 0.5;
                if (++Diffuser.Cursor >= Diffuser.Line.length) Diffuser.Cursor = 0;
                Sum = Held - Sum;
            }
            Outs[Side][Index] = Dry[Side][Index] * (1 - Wet * 0.5) + Sum * Wet * 3;
        }
    }
}

// Struck-metal mode sets [Hz, decay s, level].
const Anvil = [[1240, 1.6, 0.6], [2330, 1.2, 0.5], [3610, 0.9, 0.35], [5220, 0.6, 0.25], [7040, 0.4, 0.15]];
const SteelPlate = [[620, 0.3, 1], [1450, 0.22, 0.7], [2900, 0.15, 0.5], [4300, 0.1, 0.3]];
const Pipe = (Hz) => [[Hz, 1.6, 1], [Hz * 2.756, 1.2, 0.7], [Hz * 5.404, 0.9, 0.5], [Hz * 8.933, 0.6, 0.3]];   // free-free beam ratios
const PressBody = [[210, 0.6, 1], [470, 0.5, 0.7], [980, 0.35, 0.5], [1650, 0.25, 0.35], [2730, 0.18, 0.25]];

//------------------------------------------------------------------------------------------------------------------------
//                                                    CUES
//------------------------------------------------------------------------------------------------------------------------

export const Cues = {
    // ── Factory ─────────────────────────────────────────────────────────────────────────────────────────────────────
    Conveyor: {
        Group: 'Factory', Label: 'Conveyor line', Seconds: 8, Loop: true, Room: { Wet: 0.22 },
        Draw(Left, Right, Rate, { Seconds = 8 } = {})
        {
            const Lap = 8;
            // Drive: 4-pole motor at ~1440 rpm (24 Hz) with rotor-slot whine, helical reducer mesh (23 teeth, 552 Hz),
            // and the motor's 100 Hz magnetic hum.
            Motor(Left, Right, Rate, 0, Seconds, { Hz: () => 24, Tones: [[1, 0.3], [23, 0.09], [46, 0.025], [28, 0.02]], Gain: 0.35, Pan: -0.55, Seed: 401 });
            Hum(Left, Right, Rate, 0, Seconds, { Hz: 100, Harmonics: [1, 0.35, 0.12], Gain: 0.035, Pan: -0.55 });
            // Roller bed rumble and the drive chain rattling at its link rate (~30 Hz).
            Air(Left, Right, Rate, 0, Seconds, { Low: 70, High: 320, Gain: 0.16, Seed: 402 });
            const Noise = new SeededNoise(403), Chain = new Biquad(Rate).Shape('Band', 2400, 1.2);
            Span(Left, Right, Rate, 0, Seconds, (Age) => Chain.Run(Noise.White()) * Math.pow(0.5 + 0.5 * Math.sin(Tau * 30 * Age), 6) * 0.08, -0.4);
            // One roller with a dry bearing squeaks once a turn (26 turns a lap, so the loop stays in step).
            for (let Turn = 0; Turn * Lap / 26 < Seconds; Turn++)
                Whine(Left, Right, Rate, Turn * Lap / 26 + 0.01, 0.07, { From: 2350, To: 2150, Gain: 0.03, Harmonics: [1, 0.3, 0.1], Curve: 1, Pan: 0.45 });
            // Belt splice clacks over the head roller twice a lap; boxes thud over the transfer plate and rattle.
            for (const Time of [1.0, 5.0]) Strike(Left, Right, Rate, Time, { Modes: [[140, 0.06, 1], [520, 0.03, 0.5], [1800, 0.012, 0.3]], Burst: 0.002, Gain: 0.5, Pan: 0.1, Seed: 404 + Time });
            for (const [Time, Pan] of [[0.7, -0.3], [2.9, 0.2], [4.4, -0.1], [6.8, 0.3]])
            {
                Strike(Left, Right, Rate, Time, { Modes: [[95, 0.06, 1], [230, 0.045, 0.6], [610, 0.025, 0.3]], Burst: 0.004, Gain: 0.7, Pan, Seed: 410 + Time * 10 });
                for (let Rattle = 0; Rattle < 4; Rattle++) Strike(Left, Right, Rate, Time + 0.03 + Rattle * 0.045, { Modes: [[380 + Rattle * 70, 0.02, 1], [1300, 0.008, 0.4]], Burst: 0.001, Gain: 0.3, Pan, Seed: 420 + Rattle });
            }
        },
    },
    FactoryFloor: {
        Group: 'Factory', Label: 'Factory floor', Seconds: 10, Loop: true, Room: { Wet: 0.55, Size: 0.9 },
        Draw(Left, Right, Rate, { Seconds = 10 } = {})
        {
            // Far machinery bed: low rumble, mains hum, a big motor; distant clanks, air bursts and a reversing forklift.
            Air(Left, Right, Rate, 0, Seconds, { Low: 30, High: 180, Gain: 0.3, Seed: 431 });
            Air(Left, Right, Rate, 0, Seconds, { Low: 400, High: 2500, Gain: 0.025, Seed: 433 });
            Hum(Left, Right, Rate, 0, Seconds, { Hz: 100, Harmonics: [1, 0.3], Gain: 0.025, Pan: 0.3 });
            Motor(Left, Right, Rate, 0, Seconds, { Hz: () => 24.6, Tones: [[1, 0.4], [19, 0.08], [38, 0.02]], Gain: 0.2, Pan: -0.6, Seed: 435 });
            for (const [Time, Pan, Gain] of [[1.3, -0.7, 0.25], [3.7, 0.5, 0.4], [6.1, -0.2, 0.18], [8.4, 0.8, 0.3]])
                Strike(Left, Right, Rate, Time, { Modes: SteelPlate, Burst: 0.001, Gain, Pan, Seed: 436 + Time });
            for (const Time of [2.2, 7.3]) Air(Left, Right, Rate, Time, 0.55, { Low: 2000, High: 9000, Gain: 0.06, Pink: false, Seed: 440 + Time, Pan: 0.6, Shape: (P) => Math.min(1, P * 30) * Math.pow(1 - P, 2) });
            for (let Beep = 0; Beep < 4; Beep++) Hum(Left, Right, Rate, 4.6 + Beep * 0.5, 0.25, { Hz: 1100, Harmonics: [1, 0, 0.3, 0, 0.12], Gain: 0.02, Pan: 0.7 });
        },
    },
    Hammering: {
        Group: 'Factory', Label: 'Blacksmith hammering', Seconds: 5.6, Room: { Wet: 0.25 },
        Draw(Left, Right, Rate)
        {
            // Blows on hot steel are dull (the work damps the anvil); the hammer's bounce on the bare face rings.
            for (const [Step, Time] of [0.15, 0.62, 1.09, 1.56, 2.03].entries())
            {
                Strike(Left, Right, Rate, Time, { Modes: [[160, 0.06, 1], [380, 0.045, 0.7], [820, 0.03, 0.4], [1700, 0.015, 0.2]], Burst: 0.0015, Gain: 1, Breath: { Low: 300, High: 3000, Decay: 0.05, Gain: 0.3 }, Seed: 450 + Step });
                Strike(Left, Right, Rate, Time, { Modes: Anvil, Burst: 0.0005, Gain: 0.1, Seed: 456 + Step });
                Strike(Left, Right, Rate, Time + 0.17, { Modes: Anvil, Burst: 0.0003, Gain: 0.6, Pan: 0.1, Seed: 462 + Step });
            }
            // Three light taps on the horn, then the piece goes into the quench tank.
            for (const [Step, Time] of [2.8, 3.05, 3.3].entries()) Strike(Left, Right, Rate, Time, { Modes: Anvil.map(([Hz, Decay, Level]) => [Hz * 1.08, Decay, Level]), Burst: 0.0003, Gain: 0.9, Pan: 0.2, Seed: 470 + Step });
            Air(Left, Right, Rate, 3.95, 1.5, { Low: 1500, High: 9000, Gain: 0.07, Pink: false, Seed: 475, Shape: (P) => Math.min(1, P * 25) * Math.pow(1 - P, 1.6) });
            Crackle(Left, Right, Rate, 3.95, 1.2, { Density: (P) => 900 * (1 - P), Gain: 0.2, Low: 1200, Short: [0.0002, 0.0015], Seed: 476 });
        },
    },
    Grinder: {
        Group: 'Factory', Label: 'Angle grinder on steel', Seconds: 6.5, Room: { Wet: 0.2 },
        Draw(Left, Right, Rate, { Seconds = 6.5 } = {})
        {
            // Universal motor ~11000 rpm (183 Hz), bevel gear mesh (11 teeth), commutator (24 segments), fan; it spins
            // up, sags under load while cutting, then winds down. Contact adds the screech, chatter and sparks.
            const Sag = (Age) => 0.14 * Smoothstep(1.5, 1.8, Age) * (1 - Smoothstep(4.3, 4.6, Age));
            const Speed = (Age) => (Age < 4.8 ? Smoothstep(0.1, 0.9, Age) * (1 - Sag(Age)) : Math.exp(-(Age - 4.8) / 0.45));
            const Contact = (Age) => Smoothstep(1.5, 1.6, Age) * (1 - Smoothstep(4.3, 4.45, Age));
            Motor(Left, Right, Rate, 0, Seconds, { Hz: (Age) => 183 * Speed(Age), Tones: [[1, 0.05], [2, 0.03], [11, 0.07], [22, 0.025], [24, 0.04]], Gain: 0.5, Shape: (P, Age) => Math.pow(Speed(Age), 1.5), Seed: 480 });
            Air(Left, Right, Rate, 0, Seconds, { Low: 1500, High: 7000, Gain: 0.12, Seed: 481, Shape: (P, Age) => Speed(Age) ** 2 });
            const Noise = new SeededNoise(482), Screech = new Biquad(Rate), Grit = new Biquad(Rate).Shape('Band', 500, 0.8);
            let Wander = 0;
            Span(Left, Right, Rate, 0, Seconds, (Age) =>
            {
                const Touch = Contact(Age);
                if (Touch < 1e-3) return 0;
                Wander += (Noise.White() - Wander) * 0.0004;
                if ((Math.round(Age * Rate) & 31) === 0) Screech.Shape('Band', 3400 + Wander * 9000, 6);
                const Chatter = 0.6 + 0.4 * Math.sin(Tau * 55 * Age);         // the disc turns at ~55 Hz behind the gear
                return (Screech.Run(Noise.White()) * 0.5 + Grit.Run(Noise.White()) * 0.35 * Chatter) * Touch;
            });
            Crackle(Left, Right, Rate, 1.55, 2.85, { Density: () => 1600, Gain: 0.5, Low: 3000, Seed: 483, Pan: 0.3 });
        },
    },
    StampingPress: {
        Group: 'Factory', Label: 'Hydraulic stamping press', Seconds: 4.8, Room: { Wet: 0.3 },
        Draw(Left, Right, Rate, { Seconds = 4.8 } = {})
        {
            // Gear pump (9 teeth at 1500 rpm) strains while the ram comes down, the die closes with a thump that rings
            // the press body, the sheet shears, pressure vents, the ram returns and the part drops into the bin.
            Motor(Left, Right, Rate, 0, Seconds, { Hz: () => 25, Tones: [[1, 0.2], [9, 0.08], [18, 0.03], [27, 0.012]], Gain: 0.35, Pan: -0.3, Seed: 490, Shape: (P, Age) => 0.6 + 0.4 * Smoothstep(0.3, 0.6, Age) * (1 - Smoothstep(1.25, 1.35, Age)) + 0.3 * Smoothstep(2.3, 2.5, Age) * (1 - Smoothstep(3.1, 3.3, Age)) });
            Air(Left, Right, Rate, 0.3, 0.95, { Low: 800, High: 5000, Gain: 0.05, Seed: 491, Shape: (P) => P * P });
            Thump(Left, Right, Rate, 1.25, { Hz: 55, EndHz: 30, Decay: 0.5, Gain: 1, Drive: 1.8 });
            Strike(Left, Right, Rate, 1.25, { Modes: PressBody, Burst: 0.002, Gain: 0.5, Seed: 492 });
            Crackle(Left, Right, Rate, 1.25, 0.05, { Density: () => 9000, Gain: 0.9, Low: 1500, Short: [0.0002, 0.001], Seed: 493, Skew: 1.2 });
            Air(Left, Right, Rate, 1.6, 0.9, { Low: 1200, High: 9000, Gain: 0.14, Pink: false, Seed: 494, Shape: (P) => Math.min(1, P * 20) * Math.pow(1 - P, 1.5) });
            Bounce(Left, Right, Rate, 3.4, { Modes: SteelPlate, Hits: 4, First: 0.16, Ratio: 0.6, Gain: 0.9, Pan: 0.45, Seed: 495 });
        },
    },
    Welding: {
        Group: 'Factory', Label: 'MIG welding', Seconds: 5, Room: { Wet: 0.15 },
        Draw(Left, Right, Rate)
        {
            // Two beads: gas solenoid click and hiss, wire feeder motor, the frying crackle of short-circuit transfer
            // (droplets shorting ~100–150 times a second) with its irregular buzz.
            for (const [Bead, [Time, Lasts]] of [[0.25, 2.05], [2.75, 1.95]].entries())
            {
                Strike(Left, Right, Rate, Time - 0.08, { Modes: [[2100, 0.01, 1], [4800, 0.006, 0.5]], Burst: 0.0003, Gain: 0.6, Seed: 500 + Bead });
                Air(Left, Right, Rate, Time - 0.06, Lasts + 0.3, { Low: 2500, High: 9000, Gain: 0.025, Pink: false, Seed: 502 + Bead });
                Motor(Left, Right, Rate, Time, Lasts, { Hz: () => 60, Tones: [[15, 0.03], [30, 0.01]], Gain: 0.3, Pan: -0.5, Seed: 504 + Bead });
                Crackle(Left, Right, Rate, Time, Lasts, { Density: () => 140, Gain: 0.9, Low: 700, Short: [0.0006, 0.003], Seed: 506 + Bead, Spread: 0.2, Skew: 1.4 });
                Crackle(Left, Right, Rate, Time, Lasts, { Density: () => 2200, Gain: 0.3, Low: 1800, Short: [0.0001, 0.0008], Seed: 508 + Bead, Spread: 0.3 });
                Hum(Left, Right, Rate, Time, Lasts, { Hz: 118, Harmonics: [0.4, 0.3, 0.25, 0.2], Buzz: 0.6, Gain: 0.01, Drift: 0.06 });
            }
        },
    },
    ImpactWrench: {
        Group: 'Factory', Label: 'Air impact wrench', Seconds: 3.8, Room: { Wet: 0.2 },
        Draw(Left, Right, Rate)
        {
            // Air motor free-spins (vanes whine), then the hammer mechanism bites at ~22 blows a second, twice.
            for (const [Run, Time] of [0.2, 2.0].entries())
            {
                Air(Left, Right, Rate, Time, 1.4, { Low: 1000, High: 8000, Gain: 0.08, Pink: false, Seed: 520 + Run });
                Motor(Left, Right, Rate, Time, 1.4, { Hz: (Age) => 140 * (Age < 0.35 ? Smoothstep(0, 0.15, Age) : 0.7), Tones: [[1, 0.04], [5, 0.05], [10, 0.015]], Gain: 0.6, Seed: 522 + Run });
                for (let Blow = 0; Blow < 20; Blow++)
                    Strike(Left, Right, Rate, Time + 0.35 + Blow / 22, { Modes: [[1150, 0.03, 1], [2480, 0.02, 0.6], [3900, 0.012, 0.4], [420, 0.04, 0.5]], Burst: 0.0006, Gain: 0.35 * (0.8 + 0.2 * Math.sin(Blow * 1.7)), Seed: 530 + Run * 40 + Blow });
            }
        },
    },
    PipeDrop: {
        Group: 'Factory', Label: 'Steel pipes dropped', Seconds: 4.5, Room: { Wet: 0.3 },
        Draw(Left, Right, Rate)
        {
            // Three pipes land on concrete: a dull thud each, then bounces that ring their free-free bending modes.
            for (const [Step, [Time, Hz, Pan]] of [[0.1, 420, -0.4], [0.33, 510, 0.2], [0.5, 365, 0.5]].entries())
            {
                Strike(Left, Right, Rate, Time, { Modes: [[110, 0.05, 1], [260, 0.03, 0.5]], Burst: 0.003, Gain: 0.3, Pan, Seed: 540 + Step });
                Bounce(Left, Right, Rate, Time, { Modes: Pipe(Hz), Hits: 5, First: 0.18, Ratio: 0.55, Gain: 1.2, Pan, Seed: 545 + Step * 9 });
            }
        },
    },

    // ── Creaks ──────────────────────────────────────────────────────────────────────────────────────────────────────
    BeamCreak: {
        Group: 'Creaks', Label: 'Steel beam under load', Seconds: 5.6, Room: { Wet: 0.45, Size: 0.88 },
        Draw(Left, Right, Rate)
        {
            const Girder = [[190, 0.09], [430, 0.06], [980, 0.035], [2100, 0.02]];
            Creak(Left, Right, Rate, 0.4, 2.2, { From: 38, To: 26, Gain: 2.4, Modes: Girder, Seed: 560, Pan: -0.2 });
            Creak(Left, Right, Rate, 3.0, 1.6, { From: 46, To: 30, Gain: 2, Modes: Girder, Seed: 561, Pan: 0.2 });
            for (const [Step, Time] of [0.2, 2.8, 4.75].entries()) Strike(Left, Right, Rate, Time, { Modes: [[2900, 0.25, 1], [5400, 0.15, 0.5]], Burst: 0.0002, Gain: 0.6, Pan: -0.5 + Step * 0.5, Seed: 562 + Step });
            Strike(Left, Right, Rate, 5.0, { Modes: PressBody, Burst: 0.002, Gain: 0.35, Seed: 566 });
        },
    },
    DoorCreak: {
        Group: 'Creaks', Label: 'Heavy steel door', Seconds: 4.2, Room: { Wet: 0.3 },
        Draw(Left, Right, Rate)
        {
            // Dry hinge: stick-slip sliding down in pitch, ringing the door panel; then it slams and latches.
            Creak(Left, Right, Rate, 0.15, 2.5, { From: 520, To: 300, Gain: 3, Modes: [[340, 0.035], [780, 0.025], [1620, 0.015], [3100, 0.008]], Seed: 570, Pan: -0.3 });
            Thump(Left, Right, Rate, 3.0, { Hz: 70, EndHz: 40, Decay: 0.3, Gain: 1.8, Drive: 1.5 });
            Strike(Left, Right, Rate, 3.0, { Modes: [[180, 0.2, 1], [410, 0.15, 0.6], [930, 0.08, 0.4], [2100, 0.04, 0.2]], Burst: 0.002, Gain: 1.5, Pan: -0.3, Seed: 571 });
            Strike(Left, Right, Rate, 3.06, { Modes: [[2600, 0.02, 1], [5200, 0.01, 0.5]], Burst: 0.0003, Gain: 1.2, Pan: -0.2, Seed: 572 });
        },
    },
    FloorCreak: {
        Group: 'Creaks', Label: 'Wooden floor creaks', Seconds: 3.2, Room: { Wet: 0.2 },
        Draw(Left, Right, Rate)
        {
            const Boards = [[240, 0.025], [610, 0.015], [1350, 0.008]];
            for (const [Step, [Time, Lasts, From, To, Pan]] of [[0.1, 0.6, 190, 140, -0.4], [1.1, 0.45, 230, 170, 0], [1.95, 0.75, 160, 120, 0.4]].entries())
                Creak(Left, Right, Rate, Time, Lasts, { From, To, Gain: 3, Modes: Boards, Seed: 580 + Step, Pan });
        },
    },
    ChainHoist: {
        Group: 'Creaks', Label: 'Chain hoist lifting', Seconds: 5, Room: { Wet: 0.3 },
        Draw(Left, Right, Rate)
        {
            // Hand-chain ratchet: pawl clicks ~9 a second, links rattling over the sprocket, the load creaking the beam.
            for (let Click = 0; Click < 38; Click++)
            {
                const Time = 0.25 + Click / 9;
                Strike(Left, Right, Rate, Time, { Modes: [[3200, 0.02, 1], [5600, 0.012, 0.6]], Burst: 0.0003, Gain: 1.1, Pan: 0.2, Seed: 590 + Click });
                for (let Link = 0; Link < 2; Link++) Strike(Left, Right, Rate, Time + 0.02 + Link * 0.03 + (Click % 3) * 0.004, { Modes: [[1800 + Link * 300, 0.03, 1], [2900, 0.02, 0.5]], Burst: 0.0005, Gain: 0.45, Pan: -0.2, Seed: 640 + Click * 2 + Link });
            }
            Creak(Left, Right, Rate, 0.5, 3.8, { From: 28, To: 34, Gain: 0.9, Modes: [[170, 0.08], [400, 0.05], [900, 0.03]], Seed: 690 });
            Strike(Left, Right, Rate, 4.55, { Modes: [[700, 0.4, 1], [1700, 0.3, 0.6], [3300, 0.2, 0.4]], Burst: 0.0008, Gain: 0.4, Seed: 691 });
        },
    },

    // ── Sparks and electronics ──────────────────────────────────────────────────────────────────────────────────────
    SparkShower: {
        Group: 'Sparks', Label: 'Spark shower', Seconds: 4, Room: { Wet: 0.15 },
        Draw(Left, Right, Rate)
        {
            // A dense cloud of tiny snaps (burning particles), thinning out; hot fragments ping where they land; sizzle.
            Crackle(Left, Right, Rate, 0.05, 3.6, { Density: (P) => 2200 * Math.pow(1 - P, 1.5), Gain: 0.6, Low: 3500, Seed: 700 });
            Crackle(Left, Right, Rate, 0.05, 3.2, { Density: (P) => 160 * (1 - P), Gain: 0.9, Low: 1200, Short: [0.0004, 0.002], Seed: 701, Skew: 1.6 });
            const Noise = new SeededNoise(702);
            for (let Piece = 0; Piece < 40; Piece++) Strike(Left, Right, Rate, 0.3 + Math.pow(Noise.Next(), 1.5) * 3, { Modes: [[Noise.Range(4000, 9000), Noise.Range(0.004, 0.015), 1]], Burst: 0.0002, Gain: Noise.Range(0.2, 0.8), Pan: Noise.Range(-0.8, 0.8), Seed: 703 + Piece });
            Air(Left, Right, Rate, 0.05, 3.8, { Low: 5000, High: 14000, Gain: 0.03, Pink: false, Seed: 760, Shape: (P) => Math.min(1, P * 40) * (1 - P) });
        },
    },
    ShortCircuit: {
        Group: 'Sparks', Label: 'Short circuit and breaker trip', Seconds: 4.5, Room: { Wet: 0.25 },
        Draw(Left, Right, Rate)
        {
            // Arc buzz, a bang as the fault current flashes, fizzing, the breaker trips, smoke hiss, cooling ticks.
            Arcing(Left, Right, Rate, 0.1, 0.13, { Gain: 0.6, Density: 600, Seed: 770 });
            Hum(Left, Right, Rate, 0.1, 0.13, { Hz: 100, Harmonics: [0.5, 0.4, 0.3], Buzz: 1, Gain: 0.15 });
            Thump(Left, Right, Rate, 0.22, { Hz: 90, EndHz: 40, Decay: 0.25, Gain: 0.9, Drive: 2 });
            Crackle(Left, Right, Rate, 0.22, 0.03, { Density: () => 9000, Gain: 1, Low: 800, Short: [0.0002, 0.001], Seed: 771, Skew: 1 });
            Crackle(Left, Right, Rate, 0.25, 2.2, { Density: (P) => 900 * (1 - P) ** 2, Gain: 0.45, Low: 2000, Short: [0.0002, 0.002], Seed: 772 });
            Arcing(Left, Right, Rate, 0.25, 1.6, { Gain: 0.2, Density: 40, Seed: 773 });
            Strike(Left, Right, Rate, 0.31, { Modes: [[180, 0.04, 1], [650, 0.03, 0.7], [2300, 0.012, 0.4]], Burst: 0.001, Gain: 0.8, Pan: 0.5, Seed: 774 });
            Air(Left, Right, Rate, 0.3, 3.2, { Low: 3000, High: 9000, Gain: 0.02, Pink: false, Seed: 775, Shape: (P) => Math.min(1, P * 30) * (1 - P) });
            const Noise = new SeededNoise(776);
            for (let Tick = 0; Tick < 6; Tick++) Strike(Left, Right, Rate, 2.5 + Tick * Noise.Range(0.2, 0.4), { Modes: [[Noise.Range(2500, 6000), 0.01, 1]], Burst: 0.0002, Gain: 0.35, Pan: Noise.Range(-0.5, 0.5), Seed: 777 + Tick });
        },
    },
    ArcBuzz: {
        Group: 'Sparks', Label: 'High-voltage arc', Seconds: 5, Room: { Wet: 0.2 },
        Draw(Left, Right, Rate)
        {
            // A climbing arc (Jacob's ladder): harsh 100 Hz buzz rich in harmonics with crackle, getting brighter as it
            // stretches, then it snaps and restarts at the bottom. Three climbs.
            for (const [Climb, Time] of [0.1, 1.75, 3.4].entries())
            {
                Hum(Left, Right, Rate, Time, 1.5, { Hz: 100, Harmonics: [1, 0.7, 0.55, 0.45, 0.35, 0.3, 0.25, 0.2, 0.16, 0.12], Buzz: 0.6, Gain: 0.12, Shape: (P) => Math.min(1, P * 40) * (0.6 + 0.4 * P), Drift: 0.01 });
                Crackle(Left, Right, Rate, Time, 1.5, { Density: (P) => 300 + 500 * P, Gain: 0.35, Low: 2500, Seed: 790 + Climb });
                Crackle(Left, Right, Rate, Time + 1.48, 0.04, { Density: () => 8000, Gain: 0.9, Low: 900, Short: [0.0002, 0.001], Seed: 795 + Climb, Skew: 1 });
            }
        },
    },
    TransformerHum: {
        Group: 'Sparks', Label: 'Transformer hum', Seconds: 6, Loop: true, Room: { Wet: 0.25 },
        Draw(Left, Right, Rate, { Seconds = 6 } = {})
        {
            // Magnetostriction hums at twice the 50 Hz line (100 Hz and harmonics); a loose lamination buzzes; the
            // cooling fan turns over.
            Hum(Left, Right, Rate, 0, Seconds, { Hz: 100, Harmonics: [1, 0.45, 0.3, 0.2, 0.12, 0.08], Buzz: 0.06, Gain: 0.12 });
            Hum(Left, Right, Rate, 0, Seconds, { Hz: 300.4, Harmonics: [0.2], Gain: 0.05, Pan: 0.3 });
            Motor(Left, Right, Rate, 0, Seconds, { Hz: () => 23, Tones: [[7, 0.02], [14, 0.006]], Gain: 0.4, Pan: 0.5, Seed: 800 });
            Air(Left, Right, Rate, 0, Seconds, { Low: 400, High: 2500, Gain: 0.035, Seed: 801, Pan: 0.5 });
        },
    },
    RelayClicks: {
        Group: 'Sparks', Label: 'Contactor and relay bank', Seconds: 3.4, Room: { Wet: 0.12 },
        Draw(Left, Right, Rate)
        {
            // Big contactor pulls in (clunk, brief coil buzz), eight relays click on in turn (each contact bounces),
            // then drop out in reverse with a lower clack.
            Strike(Left, Right, Rate, 0.1, { Modes: [[210, 0.03, 1], [820, 0.02, 0.6], [2600, 0.01, 0.4]], Burst: 0.001, Gain: 0.7, Seed: 810 });
            Hum(Left, Right, Rate, 0.1, 0.4, { Hz: 100, Harmonics: [0.4, 0.3], Buzz: 0.8, Gain: 0.02 });
            for (let Relay = 0; Relay < 8; Relay++)
            {
                const Pan = -0.8 + Relay * 0.23;
                for (const [Delay, Gain] of [[0, 2.4], [0.0012, 0.9], [0.0027, 0.35]]) Strike(Left, Right, Rate, 0.6 + Relay * 0.11 + Delay, { Modes: [[2600, 0.008, 1], [5100, 0.005, 0.6]], Burst: 0.0002, Gain, Pan, Seed: 812 + Relay * 3 });
                Strike(Left, Right, Rate, 2.0 + (7 - Relay) * 0.08, { Modes: [[1900, 0.01, 1], [4200, 0.006, 0.5]], Burst: 0.0003, Gain: 1.6, Pan, Seed: 840 + Relay });
            }
        },
    },
    ServoArm: {
        Group: 'Sparks', Label: 'Robot arm servos', Seconds: 5.5, Room: { Wet: 0.2 },
        Draw(Left, Right, Rate)
        {
            // Four moves: servo motors whir with joint speed through their gearheads, a brake clicks at each stop, the
            // PWM drive sings faintly while holding; the gripper closes with a pneumatic psst and clack.
            for (const [Step, [Time, Lasts, Reach]] of [[0.2, 1.1, 1], [1.5, 0.8, 0.6], [2.6, 1.4, 0.85], [4.25, 0.7, 0.5]].entries())
            {
                const Speed = (Age, P) => Math.sin(Math.PI * P) ** 0.7 * Reach;
                Motor(Left, Right, Rate, Time, Lasts, { Hz: (Age, P) => 40 + 110 * Speed(Age, P), Tones: [[1, 0.02], [6, 0.05], [12, 0.02], [30, 0.012]], Gain: 0.6, Shape: (P, Age) => 0.25 + 0.75 * Speed(Age, P), Pan: -0.4 + Step * 0.25, Seed: 850 + Step });
                Strike(Left, Right, Rate, Time + Lasts, { Modes: [[1500, 0.012, 1], [3700, 0.006, 0.5]], Burst: 0.0003, Gain: 0.9, Pan: -0.4 + Step * 0.25, Seed: 860 + Step });
            }
            Hum(Left, Right, Rate, 0.1, 5.2, { Hz: 8000, Harmonics: [1], Gain: 0.0025 });
            Air(Left, Right, Rate, 5.0, 0.2, { Low: 1500, High: 9000, Gain: 0.1, Pink: false, Seed: 870, Shape: (P) => Math.min(1, P * 20) * (1 - P) });
            Strike(Left, Right, Rate, 5.18, { Modes: [[900, 0.03, 1], [2400, 0.015, 0.5]], Burst: 0.0006, Gain: 0.4, Seed: 871 });
        },
    },
    FluorescentFlicker: {
        Group: 'Sparks', Label: 'Fluorescent tube starting', Seconds: 5, Room: { Wet: 0.15 },
        Draw(Left, Right, Rate)
        {
            // The starter's bimetal ticks, the tube flickers with bursts of ballast buzz, strikes with a faint ting,
            // then settles into a steady 100 Hz hum with an occasional flicker tick.
            for (const [Step, Time] of [0.1, 0.45, 0.9, 1.3].entries())
            {
                Strike(Left, Right, Rate, Time, { Modes: [[3100, 0.008, 1], [6200, 0.004, 0.4]], Burst: 0.0002, Gain: 0.8, Seed: 880 + Step });
                Hum(Left, Right, Rate, Time + 0.02, 0.07 + Step * 0.02, { Hz: 100, Harmonics: [0.5, 0.6, 0.4, 0.3, 0.2], Buzz: 0.5, Gain: 0.08 });
            }
            Strike(Left, Right, Rate, 1.5, { Modes: [[2400, 0.3, 0.4], [5200, 0.2, 0.3]], Burst: 0.0002, Gain: 0.4, Seed: 886 });
            Hum(Left, Right, Rate, 1.5, 3.5, { Hz: 100, Harmonics: [0.5, 0.6, 0.4, 0.3, 0.2, 0.12], Buzz: 0.25, Gain: 0.05, Shape: (P, Age) => (Math.abs(Age - 2.1) < 0.03 || Math.abs(Age - 3.4) < 0.02 ? 0.3 : 1) * Math.min(1, P * 50) });
        },
    },
    CoilWhine: {
        Group: 'Sparks', Label: 'Electronics coil whine', Seconds: 4.5, Room: { Wet: 0.05 }, Peak: 0.4,
        Draw(Left, Right, Rate)
        {
            // Inductors singing under a changing load: the whine jumps between pitches as the load steps, with a chirp
            // at each change; the power supply's 100 Hz buzz and a small fan underneath.
            const Steps = [[0, 7800, 0.6], [0.5, 11200, 0.9], [0.9, 9300, 0.4], [1.6, 11200, 1], [2.3, 6900, 0.5], [2.8, 12400, 0.8], [3.5, 8100, 0.6]];
            let Phase = 0;
            Span(Left, Right, Rate, 0.1, 4.2, (Age, Progress) =>
            {
                let Slot = 0;
                while (Slot + 1 < Steps.length && Age >= Steps[Slot + 1][0]) Slot++;
                const [Since, Hz, Load] = Steps[Slot], Chirp = Age - Since < 0.03 ? 1 + (0.03 - (Age - Since)) * 12 : 1;
                Phase = (Phase + Hz * Chirp / Rate) % 1;
                return Math.sin(Tau * Phase) * 0.05 * Load * Edges(Progress, 4.2);
            });
            Hum(Left, Right, Rate, 0.1, 4.2, { Hz: 100, Harmonics: [0.3, 0.2, 0.1], Buzz: 0.3, Gain: 0.01 });
            Air(Left, Right, Rate, 0.1, 4.2, { Low: 300, High: 2000, Gain: 0.03, Seed: 890 });
        },
    },
    LooseWire: {
        Group: 'Sparks', Label: 'Loose wire sparking', Seconds: 4.5, Room: { Wet: 0.15 },
        Draw(Left, Right, Rate)
        {
            // A bad connection makes and breaks: irregular bursts of arc buzz and crackle, each starting with a tick.
            for (const [Step, [Time, Lasts]] of [[0.2, 0.08], [0.6, 0.3], [1.3, 0.05], [1.5, 0.5], [2.6, 0.12], [3.0, 0.7], [4.0, 0.06]].entries())
            {
                Strike(Left, Right, Rate, Time, { Modes: [[2800, 0.006, 1]], Burst: 0.0002, Gain: 1.2, Seed: 900 + Step });
                Arcing(Left, Right, Rate, Time, Lasts, { Gain: 0.4, Density: 400, Seed: 910 + Step });
                Hum(Left, Right, Rate, Time, Lasts, { Hz: 100, Harmonics: [0.4, 0.35, 0.3, 0.2], Buzz: 0.9, Gain: 0.08, Drift: 0.05 });
                Crackle(Left, Right, Rate, Time, Lasts, { Density: () => 700, Gain: 0.5, Low: 2000, Seed: 920 + Step, Spread: 0.3 });
            }
        },
    },
};

/**
 * Renders one cue: draw, place it in the hall, then (loops) fold the tail over the start with a linear crossfade
 * so the buffer repeats without a seam (the machine tones repeat exactly over the loop length, so they add in phase and an
 * equal-power fade would bump them +3 dB), or (one-shots) normalize to the cue's peak and fade the last 80 ms.
 */
export function RenderCue(Name, Rate, Options = {})
{
    const Cue = Cues[Name];
    if (!Cue) throw new Error(`Unknown cue ${Name}`);
    const Seconds = Options.Seconds ?? Cue.Seconds, Overlap = Cue.Loop ? 0.5 : 0;
    let Left = new Float32Array(Math.ceil((Seconds + Overlap) * Rate)), Right = new Float32Array(Left.length);
    Cue.Draw(Left, Right, Rate, { ...Options, Seconds: Seconds + Overlap });
    if (Cue.Room) Hall(Left, Right, Rate, Cue.Room);
    if (Cue.Loop)
    {
        const Count = Math.round(Seconds * Rate), Fold = Left.length - Count;
        const FoldedL = Left.slice(0, Count), FoldedR = Right.slice(0, Count);
        for (let Index = 0; Index < Fold; Index++)
        {
            const In = Index / Fold, Out = 1 - In;
            FoldedL[Index] = Left[Index] * In + Left[Count + Index] * Out; FoldedR[Index] = Right[Index] * In + Right[Count + Index] * Out;
        }
        Left = FoldedL; Right = FoldedR;
    }
    let Loudest = 0;
    for (let Index = 0; Index < Left.length; Index++) Loudest = Math.max(Loudest, Math.abs(Left[Index]), Math.abs(Right[Index]));
    const Scale = Loudest > 0 ? (Options.Peak ?? Cue.Peak ?? 0.9) / Loudest : 1, Fade = Cue.Loop ? 0 : Math.round(0.08 * Rate);
    for (let Index = 0; Index < Left.length; Index++)
    {
        const Tail = Fade ? Math.min(1, (Left.length - 1 - Index) / Fade) : 1;
        Left[Index] *= Scale * Tail; Right[Index] *= Scale * Tail;
    }
    return { Left, Right, Seconds };
}
