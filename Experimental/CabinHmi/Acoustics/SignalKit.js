// SIGNAL KIT
// 🧩 Sample-level DSP pieces shared by every vehicle sound: seeded noise, one-pole and biquad filters, fractional delay
//    lines, parameter glides and a soft clipper. Plain JS with no WebAudio nodes, so the same code runs inside an
//    AudioWorklet (live) and in Node (offline renders to WAV).
// 📝 Every filter is a small class with Run(Input) → Output called once per sample; coefficients are recomputed only
//    when a Set…() call changes them.

export const Tau = Math.PI * 2;
export const Clamp = (Number, Low, High) => (Number < Low ? Low : Number > High ? High : Number);
export const Mix = (From, To, Amount) => From + (To - From) * Amount;
export const Smoothstep = (Low, High, Number) => { const T = Clamp((Number - Low) / (High - Low), 0, 1); return T * T * (3 - 2 * T); };
export const Decibels = (Gain) => Math.pow(10, Gain / 20);

//------------------------------------------------------------------------------------------------------------------------
//                                                    NOISE
//------------------------------------------------------------------------------------------------------------------------

/** Seeded xorshift noise. White() is uniform in [-1, 1); Gauss() is roughly normal; Pink() is −3 dB per octave. */
export class SeededNoise
{
    constructor(Seed = 1)
    {
        this.Bits = (Seed * 2654435761) >>> 0 || 1;
        this.Pinks = new Float64Array(7);
    }

    Next()
    {
        let Bits = this.Bits;
        Bits ^= Bits << 13; Bits >>>= 0;
        Bits ^= Bits >>> 17;
        Bits ^= Bits << 5; Bits >>>= 0;
        this.Bits = Bits;
        return Bits / 4294967296;
    }

    White() { return this.Next() * 2 - 1; }
    Gauss() { return (this.Next() + this.Next() + this.Next() + this.Next() - 2) * 1.73; }
    Range(Low, High) { return Low + this.Next() * (High - Low); }

    /** Paul Kellet's economy pink filter over white noise. */
    Pink()
    {
        const White = this.White(), P = this.Pinks;
        P[0] = 0.99886 * P[0] + White * 0.0555179;
        P[1] = 0.99332 * P[1] + White * 0.0750759;
        P[2] = 0.96900 * P[2] + White * 0.1538520;
        P[3] = 0.86650 * P[3] + White * 0.3104856;
        P[4] = 0.55000 * P[4] + White * 0.5329522;
        P[5] = -0.7616 * P[5] - White * 0.0168980;
        const Out = P[0] + P[1] + P[2] + P[3] + P[4] + P[5] + P[6] + White * 0.5362;
        P[6] = White * 0.115926;
        return Out * 0.11;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FILTERS
//------------------------------------------------------------------------------------------------------------------------

/** One-pole low-pass (and its complementary high-pass). */
export class OnePole
{
    constructor(Rate, Hz = 1000) { this.Rate = Rate; this.Last = 0; this.Set(Hz); }
    Set(Hz) { this.Hz = Hz; this.Coefficient = 1 - Math.exp(-Tau * Math.min(Hz, this.Rate * 0.45) / this.Rate); return this; }
    Run(Input) { this.Last += (Input - this.Last) * this.Coefficient; return this.Last; }
    High(Input) { return Input - this.Run(Input); }
}

/** RBJ cookbook biquad, transposed direct form II. */
export class Biquad
{
    constructor(Rate) { this.Rate = Rate; this.B0 = 1; this.B1 = 0; this.B2 = 0; this.A1 = 0; this.A2 = 0; this.Z1 = 0; this.Z2 = 0; }

    Shape(Type, Hz, Q = 0.707, Gain = 0)
    {
        const Omega = Tau * Clamp(Hz, 5, this.Rate * 0.47) / this.Rate;
        const Cos = Math.cos(Omega), Alpha = Math.sin(Omega) / (2 * Q), Amplitude = Math.pow(10, Gain / 40);
        let B0, B1, B2, A0, A1, A2;
        switch (Type)
        {
            case 'Low':  B0 = (1 - Cos) / 2; B1 = 1 - Cos; B2 = B0; A0 = 1 + Alpha; A1 = -2 * Cos; A2 = 1 - Alpha; break;
            case 'High': B0 = (1 + Cos) / 2; B1 = -(1 + Cos); B2 = B0; A0 = 1 + Alpha; A1 = -2 * Cos; A2 = 1 - Alpha; break;
            case 'Band': B0 = Alpha; B1 = 0; B2 = -Alpha; A0 = 1 + Alpha; A1 = -2 * Cos; A2 = 1 - Alpha; break;  // 0 dB peak
            case 'Peak': B0 = 1 + Alpha * Amplitude; B1 = -2 * Cos; B2 = 1 - Alpha * Amplitude; A0 = 1 + Alpha / Amplitude; A1 = -2 * Cos; A2 = 1 - Alpha / Amplitude; break;
            case 'LowShelf':
            case 'HighShelf':
            {
                const Root = 2 * Math.sqrt(Amplitude) * Alpha, Sign = Type === 'LowShelf' ? 1 : -1;
                B0 = Amplitude * ((Amplitude + 1) - Sign * (Amplitude - 1) * Cos + Root);
                B1 = Sign * 2 * Amplitude * ((Amplitude - 1) - Sign * (Amplitude + 1) * Cos);
                B2 = Amplitude * ((Amplitude + 1) - Sign * (Amplitude - 1) * Cos - Root);
                A0 = (Amplitude + 1) + Sign * (Amplitude - 1) * Cos + Root;
                A1 = -Sign * 2 * ((Amplitude - 1) + Sign * (Amplitude + 1) * Cos);
                A2 = (Amplitude + 1) + Sign * (Amplitude - 1) * Cos - Root;
                break;
            }
            default: throw new Error(`Unknown biquad shape ${Type}`);
        }
        this.B0 = B0 / A0; this.B1 = B1 / A0; this.B2 = B2 / A0; this.A1 = A1 / A0; this.A2 = A2 / A0;
        return this;
    }

    Run(Input)
    {
        const Out = this.B0 * Input + this.Z1;
        this.Z1 = this.B1 * Input - this.A1 * Out + this.Z2;
        this.Z2 = this.B2 * Input - this.A2 * Out;
        return Out;
    }

    Reset() { this.Z1 = this.Z2 = 0; }
}

/** Two-pole resonator tuned by frequency and decay time (seconds to −60 dB); a struck mode of a panel or a part. */
export class Resonator
{
    constructor(Rate, Hz, Decay)
    {
        this.Rate = Rate; this.Y1 = 0; this.Y2 = 0;
        this.Tune(Hz, Decay);
    }

    Tune(Hz, Decay)
    {
        const Radius = Math.exp(-6.9078 / (Decay * this.Rate));
        this.C1 = 2 * Radius * Math.cos(Tau * Clamp(Hz, 1, this.Rate * 0.47) / this.Rate);
        this.C2 = -Radius * Radius;
        this.Norm = (1 - Radius * Radius) * 0.5;
        return this;
    }

    Run(Input)
    {
        const Out = Input * this.Norm + this.C1 * this.Y1 + this.C2 * this.Y2;
        this.Y2 = this.Y1; this.Y1 = Out;
        return Out;
    }
}

/** A small bank of struck modes [[Hz, decay seconds, level], …] normalized so a unit hit rings at about unit amplitude. */
export class ModeBank
{
    constructor(Rate, Modes)
    {
        this.Rings = Modes.map(([Hz, Decay]) => new Resonator(Rate, Hz, Decay));
        this.Gains = Modes.map(([Hz, , Level = 1], Index) => Level * Math.sin(Tau * Math.min(Hz, Rate * 0.47) / Rate) / this.Rings[Index].Norm);
    }
    Run(Hit) { let Out = 0; for (let Index = 0; Index < this.Rings.length; Index++) Out += this.Rings[Index].Run(Hit) * this.Gains[Index]; return Out; }
}

/** DC blocker: a one-zero, one-pole high-pass around 10–20 Hz. */
export class DcBlock
{
    constructor(Rate, Hz = 15) { this.Pole = Math.exp(-Tau * Hz / Rate); this.In = 0; this.Out = 0; }
    Run(Input) { this.Out = Input - this.In + this.Pole * this.Out; this.In = Input; return this.Out; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DELAY
//------------------------------------------------------------------------------------------------------------------------

/** Circular delay line with a linearly interpolated fractional read. */
export class DelayLine
{
    constructor(Length)
    {
        this.Length = 1 << Math.ceil(Math.log2(Math.max(4, Length + 2)));
        this.Mask = this.Length - 1;
        this.Store = new Float32Array(this.Length);
        this.Head = 0;
    }

    Write(Input) { this.Store[this.Head] = Input; this.Head = (this.Head + 1) & this.Mask; }

    Read(Delay)
    {
        const Position = this.Head - 1 - Clamp(Delay, 0, this.Length - 3);
        const Whole = Math.floor(Position), Part = Position - Whole;
        const A = this.Store[Whole & this.Mask], B = this.Store[(Whole + 1) & this.Mask];
        return A + (B - A) * Part;
    }
}

/**
 * A pipe as a lossy waveguide: the pressure wave travels to the open end and back, reflecting with the given sign and
 * a low-pass loss each round trip. Resonances fall at multiples of Speed / (2 · Length).
 */
export class PipeSection
{
    constructor(Rate, Length, { Speed = 520, Reflect = -0.55, Loss = 3200 } = {})
    {
        this.Rate = Rate;
        this.Line = new DelayLine(Math.ceil(Rate * 2 * 4 / 300));
        this.Damping = new OnePole(Rate, Loss);
        this.Reflect = Reflect;
        this.Tune(Length, Speed);
    }

    Tune(Length, Speed) { this.Delay = (2 * Length / Speed) * this.Rate; return this; }

    Run(Input)
    {
        const Returned = this.Damping.Run(this.Line.Read(this.Delay)) * this.Reflect;
        const Out = Input + Returned;
        this.Line.Write(Out);
        return Out;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTROL AND OUTPUT
//------------------------------------------------------------------------------------------------------------------------

/** Exponential slew towards a target, for parameters that must not step (gain, cut-off, pitch). */
export class Slew
{
    constructor(Rate, Seconds = 0.03, Start = 0) { this.Current = Start; this.Target = Start; this.Speed(Rate, Seconds); }
    Speed(Rate, Seconds) { this.Coefficient = 1 - Math.exp(-1 / (Math.max(1e-4, Seconds) * Rate)); return this; }
    Run() { this.Current += (this.Target - this.Current) * this.Coefficient; return this.Current; }
}

/** Smoothed random walk in [-1, 1], used for wandering pitch, gusts and chatter. */
export class Wander
{
    constructor(Rate, Hz, Seed = 7) { this.Noise = new SeededNoise(Seed); this.Low = new OnePole(Rate, Hz); this.Second = new OnePole(Rate, Hz); this.Gain = Math.sqrt(Rate / Hz) * 0.55; }
    Run() { return Clamp(this.Second.Run(this.Low.Run(this.Noise.White())) * this.Gain, -1, 1); }
}

/**
 * A real-world tone rather than a test tone: a sine plus a narrow band of noise around it (Width: bandwidth as a share
 * of the pitch), a slow pitch drift (Jitter, ±share) and level flutter. Turbo whistles, blower and gear whines and
 * motor orders are aero- or mechano-acoustic, so their energy is smeared around the nominal pitch; a pure sine at a
 * locked pitch is what makes a synthesized whine sound electronic. Breath 0 = pure sine, 1 = pure noise band. The noise
 * band is level-normalized with a slow AGC so it sits at the sine's RMS whatever its pitch and width.
 */
export class NarrowTone
{
    constructor(Rate, { Breath = 0.35, Width = 0.02, Jitter = 0.003, Flutter = 0.12, Seed = 1, Phase = 0 } = {})
    {
        this.Rate = Rate; this.Breath = Breath; this.Width = Width; this.Jitter = Jitter; this.Flutter = Flutter;
        this.Noise = new SeededNoise(Seed); this.Phase = Phase; this.Count = 0; this.Power = 0.05; this.Shown = 0;
        this.Ring = new Resonator(Rate, 1000, 0.05);
        this.Drift = new Wander(Rate, 3.5, Seed + 1); this.Wave = new Wander(Rate, 9, Seed + 2);
        this.Track = 1 - Math.exp(-1 / (0.05 * Rate));
    }

    Run(Hz)
    {
        if (Hz <= 1 || Hz >= this.Rate * 0.45) { this.Shown = 0; return 0; }
        if ((this.Count++ & 15) === 0) this.Ring.Tune(Hz, 6.9078 / (Math.PI * Math.max(4, this.Width * Hz)));
        this.Phase += Hz * (1 + this.Jitter * this.Drift.Run()) / this.Rate;
        this.Phase -= Math.floor(this.Phase);
        let Band = 0;
        if (this.Breath > 0)
        {
            const Raw = this.Ring.Run(this.Noise.White());
            this.Power += (Raw * Raw - this.Power) * this.Track;
            Band = Raw * 0.707 / Math.sqrt(this.Power + 1e-12);
        }
        this.Shown = (Math.sin(Tau * this.Phase) * (1 - this.Breath) + Band * this.Breath) * (1 + this.Flutter * this.Wave.Run());
        return this.Shown;
    }
}

/** PolyBLEP correction for a unit step at phase T (0..1) with phase increment Dt. */
export function Blep(T, Dt)
{
    if (T < Dt) { T /= Dt; return T + T - T * T - 1; }
    if (T > 1 - Dt) { T = (T - 1) / Dt; return T * T + T + T + 1; }
    return 0;
}

/** Band-limited sawtooth (PolyBLEP), −1..1: harmonic-rich without the aliasing hash of a naive ramp. */
export class PolySaw
{
    constructor(Phase = 0) { this.Phase = Phase; }
    Run(Hz, Rate)
    {
        const Dt = Math.min(0.45, Math.max(0, Hz / Rate));
        this.Phase += Dt; if (this.Phase >= 1) this.Phase -= 1;
        return 2 * this.Phase - 1 - Blep(this.Phase, Dt);
    }
}

/** Soft clipper with unity slope at zero. */
export const SoftClip = (Input, Drive = 1) => Math.tanh(Input * Drive) / Drive;

/** Equal-power pan: −1 left … 1 right → [Left, Right] gains. */
export function PanGains(Pan)
{
    const Angle = (Clamp(Pan, -1, 1) + 1) * Math.PI / 4;
    return [Math.cos(Angle), Math.sin(Angle)];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE-SHOT STRIKES
//------------------------------------------------------------------------------------------------------------------------

/**
 * Adds a struck object into a stereo buffer at Time: a short excitation (click or noise burst) rings a set of modes.
 * Modes: [[Hz, Decay seconds, Gain], …]. Burst: noise excitation length in seconds (0 = single impulse).
 * Breath: extra filtered noise layer { Low, High, Decay, Gain } for air and scrape. Used by every mechanical cue.
 */
export function Strike(Left, Right, Rate, Time, { Modes = [], Burst = 0.0006, Gain = 1, Pan = 0, Breath = null, Seed = 1, Attack = 0 } = {})
{
    const Noise = new SeededNoise(Seed);
    const Start = Math.round(Time * Rate);
    const Longest = Math.max(Burst, ...Modes.map((Mode) => Mode[1]), Breath ? Breath.Decay * 1.2 : 0);
    const Count = Math.min(Left.length - Start, Math.ceil(Longest * Rate) + 64);
    if (Count <= 0) return;
    const Rings = Modes.map(([Hz, Decay]) => new Resonator(Rate, Hz, Decay));
    const Levels = Modes.map((Mode) => Mode[2]);
    const BurstSamples = Math.max(1, Math.round(Burst * Rate));
    const AttackSamples = Math.max(1, Math.round(Attack * Rate));
    let BreathHigh = null, BreathLow = null;
    if (Breath) { BreathHigh = new Biquad(Rate).Shape('High', Breath.Low, 0.7); BreathLow = new Biquad(Rate).Shape('Low', Breath.High, 0.7); }
    const [GainL, GainR] = PanGains(Pan);
    for (let Index = 0; Index < Count; Index++)
    {
        const Envelope = Index < BurstSamples ? (1 - Index / BurstSamples) : 0;
        const Excite = BurstSamples === 1 ? (Index === 0 ? 1 : 0) : Noise.White() * Envelope;
        let Out = 0;
        for (let Mode = 0; Mode < Rings.length; Mode++) Out += Rings[Mode].Run(Excite) * Levels[Mode];
        if (Breath)
        {
            const Shape = Math.min(1, Index / AttackSamples) * Math.exp(-Index / (Breath.Decay * Rate / 6.9));
            Out += BreathLow.Run(BreathHigh.Run(Noise.White())) * Shape * Breath.Gain;
        }
        Out *= Gain;
        Left[Start + Index] += Out * GainL;
        Right[Start + Index] += Out * GainR;
    }
}
