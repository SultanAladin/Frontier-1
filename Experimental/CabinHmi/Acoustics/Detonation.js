// DETONATION
// 🧩 The shared physics for gunfire and explosions (Follow-up 20): the blast pulse (Friedlander positive and negative
//    phase, its ground reflection a few milliseconds later, the clipping a real recording of it shows), the propellant
//    gas jet, the low "punch", an explosion scaled by charge size (blast duration grows with the cube root of the
//    charge), and the space it happens in: generated outdoor impulse responses (open field, forest, town, valley,
//    desert) applied by FFT convolution, so every report gets a believable rolling tail instead of a few slap echoes.
//    A look-ahead limiter gives the density of a recorded, mastered gun sound.
// 📝 Used by Ballistics (every shot), WarFoley and ImpactFoley (explosions). Sizes and spectra follow the usual scaling:
//    blast spectral peak near 1 / (2.5 × positive phase): a 9 mm near 1.1 kHz, a 5.56 rifle near 800 Hz, a .50 near
//    300 Hz, a grenade near 120 Hz, artillery below 50 Hz. The impulse responses are generated (velvet noise with a
//    density that rises, a decaying envelope with rolling swells, darker as it goes), not measured spaces.

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, OnePole, Biquad, PanGains, SoftClip } from './SignalKit.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONVOLUTION
//------------------------------------------------------------------------------------------------------------------------

const Twiddles = new Map();

/** In-place iterative radix-2 FFT on Re / Im (Float64Array, length a power of two). Inverse scales by 1 / N. */
export function Fft(Re, Im, Inverse = false)
{
    const Size = Re.length;
    for (let Index = 1, Swap = 0; Index < Size; Index++)
    {
        let Bit = Size >> 1;
        for (; Swap & Bit; Bit >>= 1) Swap ^= Bit;
        Swap ^= Bit;
        if (Index < Swap) { let T = Re[Index]; Re[Index] = Re[Swap]; Re[Swap] = T; T = Im[Index]; Im[Index] = Im[Swap]; Im[Swap] = T; }
    }
    if (!Twiddles.has(Size))
    {
        const Cos = new Float64Array(Size / 2), Sin = new Float64Array(Size / 2);
        for (let K = 0; K < Size / 2; K++) { Cos[K] = Math.cos(Tau * K / Size); Sin[K] = Math.sin(Tau * K / Size); }
        Twiddles.set(Size, [Cos, Sin]);
    }
    const [Cos, Sin] = Twiddles.get(Size), Sign = Inverse ? 1 : -1;
    for (let Span = 2; Span <= Size; Span <<= 1)
    {
        const Half = Span >> 1, Stride = Size / Span;
        for (let Block = 0; Block < Size; Block += Span)
            for (let K = 0; K < Half; K++)
            {
                const Wr = Cos[K * Stride], Wi = Sign * Sin[K * Stride], A = Block + K, B = A + Half;
                const Tr = Re[B] * Wr - Im[B] * Wi, Ti = Re[B] * Wi + Im[B] * Wr;
                Re[B] = Re[A] - Tr; Im[B] = Im[A] - Ti; Re[A] += Tr; Im[A] += Ti;
            }
    }
    if (Inverse) for (let Index = 0; Index < Size; Index++) { Re[Index] /= Size; Im[Index] /= Size; }
}

/**
 * Adds the stereo response of a space to a stereo buffer: the mid signal is convolved with the left and right impulse
 * responses at once (IR packed as left + i·right, so the inverse transform returns both channels).
 */
export function Convolve(Left, Right, Space, Wet = 1)
{
    const Count = Left.length, Taps = Space.Left.length;
    let Size = 1; while (Size < Count + Taps) Size <<= 1;
    const DryRe = new Float64Array(Size), DryIm = new Float64Array(Size), IrRe = new Float64Array(Size), IrIm = new Float64Array(Size);
    for (let Index = 0; Index < Count; Index++) DryRe[Index] = (Left[Index] + Right[Index]) * 0.5;
    IrRe.set(Space.Left); IrIm.set(Space.Right);
    Fft(DryRe, DryIm); Fft(IrRe, IrIm);
    for (let K = 0; K < Size; K++)
    {
        const Ar = DryRe[K], Ai = DryIm[K], Br = IrRe[K], Bi = IrIm[K];
        DryRe[K] = Ar * Br - Ai * Bi; DryIm[K] = Ar * Bi + Ai * Br;
    }
    Fft(DryRe, DryIm, true);
    for (let Index = 0; Index < Count; Index++) { Left[Index] += DryRe[Index] * Wet; Right[Index] += DryIm[Index] * Wet; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPACES
//------------------------------------------------------------------------------------------------------------------------

/**
 * Outdoor spaces. Early: discrete reflections [delay s, level, pan] (a wall, a tree line, a hillside); Density: velvet
 * impulses per second at the start and after Ramp seconds (sparse = separate echoes, dense = a smooth tail); Decay:
 * RT60 of the diffuse tail; Swells: rolling bumps in the envelope (reports rolling round hills); Bright / Dark: low-pass
 * at the start and the end (air absorbs the highs of long paths); Wet: tail energy relative to the dry sound.
 */
export const Spaces = {
    Field:  { Seconds: 2.4, Decay: 1.5, Early: [[0.038, 0.4, -0.6], [0.095, 0.3, 0.55], [0.21, 0.24, -0.25], [0.39, 0.17, 0.45]], Density: [60, 1800], Ramp: 0.7, Bright: 9500, Dark: 900, Swells: 2, Wet: 0.55 },
    Forest: { Seconds: 1.8, Decay: 1.0, Early: [[0.012, 0.35, 0.4], [0.021, 0.3, -0.5], [0.033, 0.28, 0.2], [0.05, 0.22, -0.3]], Density: [600, 5000], Ramp: 0.2, Bright: 7000, Dark: 1300, Swells: 0, Wet: 0.6 },
    Urban:  { Seconds: 2.2, Decay: 1.2, Early: [[0.016, 0.55, -0.75], [0.031, 0.5, 0.7], [0.058, 0.42, -0.4], [0.087, 0.34, 0.55], [0.13, 0.27, -0.2], [0.19, 0.2, 0.35]], Density: [180, 3500], Ramp: 0.35, Bright: 8500, Dark: 1500, Swells: 1, Wet: 0.8 },
    Valley: { Seconds: 5.0, Decay: 3.4, Early: [[0.08, 0.3, -0.4], [0.33, 0.34, 0.6], [0.78, 0.3, -0.6], [1.35, 0.25, 0.4], [2.05, 0.17, -0.2]], Density: [25, 900], Ramp: 1.6, Bright: 6000, Dark: 550, Swells: 4, Wet: 0.8 },
    Desert: { Seconds: 3.2, Decay: 2.3, Early: [[0.45, 0.22, -0.5], [1.15, 0.15, 0.6], [1.95, 0.09, 0]], Density: [10, 250], Ramp: 2, Bright: 5000, Dark: 700, Swells: 2, Wet: 0.35 },
    Range:  { Seconds: 1.4, Decay: 0.7, Early: [[0.007, 0.6, -0.8], [0.013, 0.5, 0.8], [0.022, 0.4, -0.5], [0.034, 0.32, 0.5]], Density: [1500, 6000], Ramp: 0.1, Bright: 10000, Dark: 2500, Swells: 0, Wet: 0.9 },
};

const SpaceCache = new Map();

/** Generates (and caches) the stereo impulse response of a space at a sample rate. */
export function SpaceIr(Rate, Name = 'Field', Seed = 1)
{
    const Key = `${Name}@${Rate}#${Seed}`;
    if (SpaceCache.has(Key)) return SpaceCache.get(Key);
    const S = Spaces[Name], Count = Math.round(S.Seconds * Rate), Noise = new SeededNoise(9000 + Seed * 31 + Name.length * 7);
    const Channels = [new Float32Array(Count), new Float32Array(Count)];
    const Bumps = Array.from({ length: S.Swells }, () => [Noise.Range(0.25, S.Seconds * 0.75), Noise.Range(0.12, 0.4), Noise.Range(0.5, 1.2)]);
    const Envelope = (T) => Smoothstep(0, 0.012, T) * Math.exp(-6.9 * T / S.Decay) * (1 + Bumps.reduce((Sum, [At, Width, Lift]) => Sum + Lift * Math.exp(-Math.pow((T - At) / Width, 2)), 0));
    for (const Channel of Channels)
    {
        // Velvet noise: Poisson-spaced signed impulses, density rising from sparse echoes to a smooth tail.
        for (let T = 0.004; T < S.Seconds;)
        {
            const Density = Mix(S.Density[0], S.Density[1], Clamp(T / S.Ramp, 0, 1));
            const At = Math.round(T * Rate);
            if (At < Count) Channel[At] += (Noise.Next() < 0.5 ? -1 : 1) * Envelope(T) / Math.sqrt(Density) * Noise.Range(0.6, 1.4);
            T += -Math.log(1 - Noise.Next() * 0.999) / Density;
        }
    }
    for (const [Delay, Level, Pan] of S.Early)
    {
        const [GainL, GainR] = PanGains(Pan);
        for (let Smear = 0; Smear < 4; Smear++)                             // each reflection is a short cluster, not one sample
        {
            const At = Math.round((Delay + Noise.Range(0, 0.0025)) * Rate), Part = Level * (Smear ? Noise.Range(0.15, 0.4) : 1) * (Noise.Next() < 0.5 ? -1 : 1);
            if (At < Count) { Channels[0][At] += Part * GainL * Math.SQRT2; Channels[1][At] += Part * GainR * Math.SQRT2; }
        }
    }
    for (const Channel of Channels)
    {
        // Darker as the paths get longer: two one-pole low-passes sliding from Bright to Dark; a high-pass for the DC.
        const Poles = [new OnePole(Rate, S.Bright), new OnePole(Rate, S.Bright)], Floor = new Biquad(Rate).Shape('High', 45, 0.7);
        for (let Index = 0; Index < Count; Index++)
        {
            if ((Index & 63) === 0) { const Hz = S.Bright * Math.pow(S.Dark / S.Bright, Math.sqrt(Index / Count)); Poles[0].Set(Hz); Poles[1].Set(Hz); }
            Channel[Index] = Floor.Run(Poles[1].Run(Poles[0].Run(Channel[Index])));
        }
        const Fade = Math.round(0.05 * Rate);
        for (let Index = Count - Fade; Index < Count; Index++) Channel[Index] *= (Count - Index) / Fade;
    }
    let Energy = 0;
    for (const Channel of Channels) for (const Sample of Channel) Energy += Sample * Sample;
    const Scale = Math.sqrt(S.Wet * 2 / Math.max(Energy, 1e-12));
    for (const Channel of Channels) for (let Index = 0; Index < Count; Index++) Channel[Index] *= Scale;
    const Space = { Left: Channels[0], Right: Channels[1], Name };
    SpaceCache.set(Key, Space);
    return Space;
}

/** Puts a dry buffer into a space (in place). */
export function Place(Left, Right, Rate, Name = 'Field', { Wet = 1, Seed = 1 } = {})
{
    if (!Name) return;
    Convolve(Left, Right, SpaceIr(Rate, Name, Seed), Wet);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DYNAMICS
//------------------------------------------------------------------------------------------------------------------------

/**
 * Look-ahead peak limiter: normalizes, pushes Drive (× gain) into a Ceiling, so the body and tail of a report come up
 * under the transient the way a recorded, mastered gun sound does. Attack is the look-ahead (a gain ramp before the
 * peak, no click), release exponential.
 */
export function Limit(Left, Right, Rate, { Drive = 3, Ceiling = 0.95, Release = 0.07, Look = 0.0015 } = {})
{
    const Count = Left.length, Ahead = Math.max(1, Math.round(Look * Rate));
    let Peak = 0;
    for (let Index = 0; Index < Count; Index++) Peak = Math.max(Peak, Math.abs(Left[Index]), Math.abs(Right[Index]));
    if (Peak <= 0) return;
    const Pre = Drive / Peak, Want = new Float32Array(Count), Floor = new Float32Array(Count);
    for (let Index = 0; Index < Count; Index++) { const Level = Math.max(Math.abs(Left[Index]), Math.abs(Right[Index])) * Pre; Want[Index] = Level > Ceiling ? Ceiling / Level : 1; }
    // Floor[i] = the lowest gain wanted within ±Ahead; averaging Floor over ±Ahead then gives a smooth ramp that is
    // never above what any sample needs (every Floor in the window already includes sample i).
    for (let Index = 0; Index < Count; Index++)
    {
        let Low = 1;
        for (let Step = Math.max(0, Index - Ahead), End = Math.min(Count - 1, Index + Ahead); Step <= End; Step++) if (Want[Step] < Low) Low = Want[Step];
        Floor[Index] = Low;
    }
    const Back = 1 - Math.exp(-1 / (Release * Rate)), Span = 2 * Ahead + 1;
    let Sum = 0, Gain = 1;
    for (let Index = -Ahead; Index <= Ahead; Index++) Sum += Index >= 0 && Index < Count ? Floor[Index] : 1;
    for (let Index = 0; Index < Count; Index++)
    {
        const Ramp = Sum / Span;
        Gain = Ramp < Gain ? Ramp : Gain + (Ramp - Gain) * Back;
        Left[Index] = SoftClip(Left[Index] * Gain * Pre, 1.05); Right[Index] = SoftClip(Right[Index] * Gain * Pre, 1.05);
        const Leaving = Index - Ahead, Entering = Index + Ahead + 1;
        Sum += (Entering < Count ? Floor[Entering] : 1) - (Leaving >= 0 ? Floor[Leaving] : 1);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BLAST
//------------------------------------------------------------------------------------------------------------------------

/**
 * One blast pulse (muzzle or charge): Friedlander shape (instant rise, positive phase Width seconds, a longer weaker
 * suction), its ground reflection Ground = [delay s, level] (comb colouring every real gunshot recording has), an
 * asymmetric clip (the microphone and the air both compress the positive peak, Drive), the propellant gas jet (Gas:
 * band of turbulent noise, { Seconds, Hz, Q, Gain }) and the low punch (Low: low-passed noise burst, { Seconds, Hz,
 * Gain }) instead of a pitched sine. Vary scales a random spread on width and level for shot-to-shot variation.
 */
export function Pulse(Left, Right, Rate, Time, { Width = 0.0006, Gain = 1, Drive = 2.2, Ground = [0.0026, 0.7], Gas = null, Low = null, Pan = 0, Seed = 1, Vary = 0.06, Dull = 0 } = {})
{
    const Noise = new SeededNoise(Seed), Spread = () => 1 + Noise.Gauss() * Vary;
    const W = Width * Spread(), Level = Gain * Spread(), Start = Math.round(Time * Rate);
    const Count = Math.min(Left.length - Start, Math.round((W * 14 + (Ground ? Ground[0] : 0)) * Rate) + 8);
    const [GainL, GainR] = PanGains(Pan);
    const Shape = (T) => (T < 0 ? 0 : (1 - T / W) * Math.exp(-1.35 * T / W));
    const Reflect = new OnePole(Rate, 6500), Soften = Dull ? new Biquad(Rate).Shape('Low', Dull, 0.7) : null;
    const Norm = Math.tanh(Drive);
    for (let Index = 0; Index < Count; Index++)
    {
        const T = Index / Rate;
        let P = Shape(T) + (Ground ? Reflect.Run(Shape(T - Ground[0])) * Ground[1] : 0);
        P = P > 0 ? Math.tanh(P * Drive) / Norm : P * 0.85;
        if (Soften) P = Soften.Run(P);
        Left[Start + Index] += P * Level * GainL; Right[Start + Index] += P * Level * GainR;
    }
    if (Gas)
    {
        // Gas jet: turbulent band noise with a fast exponential decay and random roughness (no filter sweep).
        const Length = Math.min(Left.length - Start, Math.round(Gas.Seconds * 6 * Rate));
        const Band = new Biquad(Rate).Shape('Band', Gas.Hz * Spread(), Gas.Q ?? 0.7), Air = new Biquad(Rate).Shape('High', 120, 0.7), Rough = new OnePole(Rate, 180);
        for (let Index = 0; Index < Length; Index++)
        {
            const T = Index / Rate, Envelope = Math.min(1, T / 0.0004) * Math.exp(-T / Gas.Seconds);
            const Out = Air.Run(Band.Run(Noise.White())) * Envelope * Gas.Gain * Level * (0.55 + Math.abs(Rough.Run(Noise.White() * 4)) * 1.6);
            Left[Start + Index] += Out * GainL * Noise.Range(0.9, 1.1); Right[Start + Index] += Out * GainR * Noise.Range(0.9, 1.1);
        }
    }
    if (Low)
    {
        // Low punch: a low-passed noise burst (two poles), fast attack, decaying over Low.Seconds.
        const Length = Math.min(Left.length - Start, Math.round(Low.Seconds * 5 * Rate));
        const Poles = [new Biquad(Rate).Shape('Low', Low.Hz, 0.9), new Biquad(Rate).Shape('Low', Low.Hz * 1.5, 0.6)];
        let Power = 0;
        const Burst = new Float32Array(Length);
        for (let Index = 0; Index < Length; Index++)
        {
            const T = Index / Rate, Envelope = Math.min(1, T / 0.0015) * Math.exp(-T / Low.Seconds);
            Burst[Index] = Poles[1].Run(Poles[0].Run(Noise.White())) * Envelope;
            Power = Math.max(Power, Math.abs(Burst[Index]));
        }
        const Scale = Low.Gain * Level / Math.max(Power, 1e-9);
        for (let Index = 0; Index < Length; Index++) { const Out = SoftClip(Burst[Index] * Scale, 1.2); Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR; }
    }
}

/**
 * Rumble: a long low roar with turbulent swells (the boiling fireball and the ground's answer), low-passed noise whose
 * level wanders at a few hertz, cut-off sliding down as it dies. Replaces the pitched sine "thump".
 */
export function Rumble(Left, Right, Rate, Time, Seconds, { Hz = 180, EndHz = 60, Gain = 0.5, Attack = 0.02, Seed = 5, Pan = 0, Width = 0.6, Swirl = 0.6 } = {})
{
    const Start = Math.round(Time * Rate), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    if (Count <= 0) return;
    const Noise = new SeededNoise(Seed), Side = new SeededNoise(Seed + 7), Swell = new OnePole(Rate, 4), Swell2 = new OnePole(Rate, 1.3);
    const Lows = [[new Biquad(Rate), new Biquad(Rate)], [new Biquad(Rate), new Biquad(Rate)]], [GainL, GainR] = PanGains(Pan);
    let Level = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Index / Count, T = Index / Rate;
        if ((Index & 63) === 0) { const Cut = Hz * Math.pow(EndHz / Hz, Progress); for (const Pair of Lows) { Pair[0].Shape('Low', Cut, 0.8); Pair[1].Shape('Low', Cut * 1.3, 0.6); } }
        const Boil = 1 + Swirl * (Swell.Run(Noise.White() * 60) + Swell2.Run(Noise.White() * 60) * 1.5);
        const Envelope = Math.min(1, T / Attack) * Math.pow(1 - Progress, 1.6) * Math.max(0.15, Boil);
        const Mid = Noise.White(), Edge = Side.White() * Width;
        const OutL = Lows[0][1].Run(Lows[0][0].Run(Mid + Edge)), OutR = Lows[1][1].Run(Lows[1][0].Run(Mid - Edge));
        Level = Envelope * Gain * 3;
        Left[Start + Index] += OutL * Level * GainL * Math.SQRT2; Right[Start + Index] += OutR * Level * GainR * Math.SQRT2;
    }
}

/**
 * An explosion's air blast at the listener: Charge in kg TNT-equivalent, Metres away. Positive phase grows with the
 * cube root of the charge and a little with range; surface bursts merge their ground reflection. Ground: 'Open'
 * (bright crack), 'Sand' (dull, longer whump: the charge throws sand instead of air), 'Rock' (sharp), 'Air'
 * (airburst: a clean double crack). Fire adds the fireball's roar (HE barely, fuel a lot). Returns the positive phase.
 */
export function Detonate(Left, Right, Rate, Time, { Charge = 1, Metres = 30, Ground = 'Open', Fire = 0.3, Pan = 0, Seed = 77, Gain = 1 } = {})
{
    const Root = Math.cbrt(Charge), Width = 0.0032 * Root * (1 + 0.18 * Math.log10(Math.max(1, Metres / 10)));
    const Sand = Ground === 'Sand', Rock = Ground === 'Rock', Airburst = Ground === 'Air';
    Pulse(Left, Right, Rate, Time, {
        Width: Width * (Sand ? 1.5 : 1), Gain: 1.2 * Gain, Drive: Sand ? 1.8 : Rock ? 3.4 : 2.8, Pan, Seed, Vary: 0.04,
        Ground: Airburst ? [0.012 * Root, 0.65] : [0.0006, 0.6], Dull: Sand ? 2400 : 0,
        Gas: { Seconds: 0.012 * Root * (Sand ? 0.6 : 1), Hz: Clamp(0.35 / Width, 120, 2600), Q: 0.6, Gain: (Rock ? 0.8 : 0.55) * Gain },
        Low: { Seconds: 0.09 * Root * (Sand ? 1.4 : 1), Hz: Clamp(0.25 / Width, 40, 160), Gain: 0.9 * Gain },
    });
    Rumble(Left, Right, Rate, Time + Width, 1.6 + 1.4 * Root, { Hz: Clamp(0.5 / Width, 90, 320), EndHz: 45, Gain: 0.35 * Gain * (Sand ? 1.25 : 1), Attack: 0.02 * Root, Seed: Seed + 1, Pan });
    if (Fire > 0)
    {
        const Noise = new SeededNoise(Seed + 2);
        Rumble(Left, Right, Rate, Time + 0.01, (1.2 + 1.5 * Root) * (0.6 + Fire), { Hz: 2200, EndHz: 220, Gain: 0.12 * Fire * Gain, Attack: 0.03, Seed: Seed + 3, Pan: Pan + Noise.Range(-0.1, 0.1), Width: 0.9, Swirl: 0.9 });
    }
    return Width;
}
