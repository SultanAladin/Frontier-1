// CASINGS
// 🧩 Spent shell casings falling (Follow-up 20 rebuild). A case is a thin brass (or steel, or plastic) tube: when it hits
//    it rings a set of inharmonic modes (free-free tube bending at 1 : 2.76 : 5.40 : 8.93 and a hoop "breathing" mode),
//    each split into a close pair because a real case is never perfectly round, so the ring shimmers and beats instead
//    of sounding like a test tone. Each contact is a Hertzian force pulse whose length depends on the floor (hard
//    concrete: tens of microseconds, so bright; a wooden deck or dirt: longer, so duller), not a burst of noise.
//    The case tumbles in flight, so it lands head or mouth first and often clips its other end a few milliseconds later
//    ("ti-tink"); the bounces follow real restitution physics and end in a fast skitter; then it rolls, the floor's grit
//    scraping the modes, the rim ticking once a turn, and it rocks to rest. The floor answers with its own sound.
// 📝 Figures are typical, not measured (a 9 mm case rings near 9 kHz, 5.56 near 6 kHz, 7.62 near 4.5 kHz, .50 BMG near
//    2.4 kHz, a 30 mm case near 1.2 kHz, a 120 mm stub near 500 Hz). Used by Ballistics for the brass after every shot
//    and by the Shells cues. Original synthesis: nothing is sampled.

import { Tau, Clamp, SeededNoise, Biquad, Resonator, PanGains, Strike } from './SignalKit.js';

const Gravity = 9.81;

/** Free-free tube bending ratios and the first hoop (breathing) mode, relative to the fundamental. */
const Tube = [1, 2.756, 5.404, 8.933];

/**
 * Case recipes. Hz: fundamental; Decay: ring time of the fundamental in seconds (higher modes die faster); Hoop: the
 * breathing mode's ratio and level; Split: how out-of-round (pair detune share); Mass scales the floor's answer;
 * Diameter (m) sets the roll rate; Roll: roll seconds on a hard floor (0 = does not roll); Plastic: a shotgun hull
 * (dull plastic body with a brass head); Gain balances the cases.
 */
export const Shells = {
    Pistol:   { Label: '9 mm pistol case',   Hz: 8900, Decay: 0.09, Hoop: [1.62, 0.5], Split: 0.006, Mass: 0.25, Diameter: 0.0099, Roll: 0.55, Gain: 1.0 },
    Rifle556: { Label: '5.56 mm rifle case', Hz: 5900, Decay: 0.16, Hoop: [1.85, 0.45], Split: 0.005, Mass: 0.4, Diameter: 0.0096, Roll: 0.75, Gain: 1.0 },
    Rifle762: { Label: '7.62 mm rifle case', Hz: 4500, Decay: 0.2, Hoop: [1.9, 0.45], Split: 0.005, Mass: 0.6, Diameter: 0.012, Roll: 0.85, Gain: 1.0 },
    Fifty:    { Label: '.50 BMG case',       Hz: 2400, Decay: 0.38, Hoop: [2.05, 0.5], Split: 0.004, Mass: 1.2, Diameter: 0.0204, Roll: 1.1, Gain: 1.1 },
    Shotgun:  { Label: '12-gauge shell',     Hz: 1650, Decay: 0.012, Hoop: [2.3, 0.3], Split: 0.02, Mass: 0.5, Diameter: 0.0207, Roll: 0.6, Gain: 1.4, Plastic: true },
    Cannon30: { Label: '30 mm cannon case',  Hz: 1220, Decay: 0.7, Hoop: [2.2, 0.55], Split: 0.003, Mass: 3, Diameter: 0.044, Roll: 0.9, Gain: 1.2 },
    TankStub: { Label: '120 mm case stub',   Hz: 510, Decay: 2.4, Hoop: [2.6, 0.6], Split: 0.003, Mass: 6, Diameter: 0.15, Roll: 0.6, Gain: 1.4 },
    Link:     { Label: 'Belt link',          Hz: 3300, Decay: 0.03, Hoop: [1.45, 0.7], Split: 0.02, Mass: 0.3, Diameter: 0.01, Roll: 0, Gain: 0.7 },
};

/**
 * Floors. Bounce: restitution; Ring: how much of the case's ring survives while it lies on the floor; Contact: Hertz
 * contact time in seconds (sets how bright the hit is); Thud: the floor's own modes and level; Grit: contact scrape
 * noise (concrete's sand, dirt's crunch); Rough: rolling scrape level (0 = the case does not roll).
 */
export const Floors = {
    Concrete: { Label: 'Concrete', Bounce: 0.36, Ring: 1, Contact: 0.00004, Thud: [[950, 0.004, 1], [2600, 0.003, 0.6], [5200, 0.002, 0.3]], ThudGain: 0.12, Grit: 0.35, Rough: 1 },
    Wood:     { Label: 'Wooden deck', Bounce: 0.28, Ring: 0.6, Contact: 0.00012, Thud: [[230, 0.05, 1], [610, 0.03, 0.6], [1180, 0.018, 0.35]], ThudGain: 0.9, Grit: 0.08, Rough: 0.55 },
    Metal:    { Label: 'Steel floor', Bounce: 0.42, Ring: 1.1, Contact: 0.00003, Thud: [[740, 0.3, 1], [1630, 0.2, 0.6], [2920, 0.14, 0.4], [4470, 0.09, 0.25]], ThudGain: 0.45, Grit: 0.05, Rough: 0.9 },
    Dirt:     { Label: 'Dirt', Bounce: 0.06, Ring: 0.06, Contact: 0.0012, Thud: [[110, 0.02, 1], [380, 0.012, 0.4]], ThudGain: 0.6, Grit: 0.7, Rough: 0 },
};

/** The modes of a case as [Hz, decay, level], each split into a pair; Muffle scales the decays (floor contact, hand). */
function CaseModes(Shell, Noise, Muffle = 1)
{
    const Modes = [];
    const Add = (Hz, Decay, Level) =>
    {
        const Detune = Shell.Split * Noise.Range(0.5, 1.5);
        Modes.push([Hz * (1 - Detune / 2), Decay, Level * 0.7], [Hz * (1 + Detune / 2), Decay * 0.9, Level * 0.55]);
    };
    Tube.forEach((Ratio, Index) => { if (Shell.Hz * Ratio < 20000) Add(Shell.Hz * Ratio * (1 + Noise.Gauss() * 0.004), Shell.Decay * Muffle / Math.pow(Ratio, 0.6), [1, 0.7, 0.45, 0.25][Index]); });
    if (Shell.Hz * Shell.Hoop[0] < 20000) Add(Shell.Hz * Shell.Hoop[0], Shell.Decay * Muffle * 0.7, Shell.Hoop[1]);
    return Modes;
}

/**
 * One contact: a half-sine force pulse Contact seconds long (Hertzian impact) drives the case's modes; Where (0 = the
 * thick head, 1 = the thin mouth) shifts the balance (the mouth rings, the head clicks). Plastic hulls get a damped
 * plastic body (low-Q) and the brass head's click. Grit adds the floor's scrape for the contact.
 */
function Clink(Left, Right, Rate, Time, Shell, Floor, Strength, { Pan = 0, Seed = 1, Where = 1, Muffle = 1 } = {})
{
    const Noise = new SeededNoise(Seed), Start = Math.round(Time * Rate);
    const Modes = Shell.Plastic ? [[Shell.Hz, 0.012, 1], [Shell.Hz * 2.3, 0.008, 0.6], [Shell.Hz * 3.7, 0.005, 0.35], [6200, 0.025, 0.5 * (1 - Where)], [9800, 0.015, 0.3 * (1 - Where)]] : CaseModes(Shell, Noise, Muffle * Floor.Ring);
    const Longest = Math.max(...Modes.map((Mode) => Mode[1])), Count = Math.min(Left.length - Start, Math.round((Longest + 0.01) * Rate));
    if (Count <= 0) return;
    const Rings = Modes.map(([Hz, Decay]) => new Resonator(Rate, Hz, Math.max(0.002, Decay)));
    const Levels = Modes.map(([Hz, , Level], Index) => Level * (Index < 2 ? Mix01(0.35, 1, Where) : 1) * Math.sin(Tau * Math.min(Hz, Rate * 0.47) / Rate) / Rings[Index].Norm);
    const Contact = Math.max(1, Math.round(Floor.Contact * (Shell.Plastic ? 3 : 1) * Rate * Noise.Range(0.8, 1.25)));
    const Scrape = new Biquad(Rate).Shape('Band', Noise.Range(3500, 7000), 0.9), [GainL, GainR] = PanGains(Pan);
    for (let Index = 0; Index < Count; Index++)
    {
        const Force = Index < Contact ? Math.sin(Math.PI * (Index + 0.5) / Contact) / Contact : 0;
        let Out = 0;
        for (let Mode = 0; Mode < Rings.length; Mode++) Out += Rings[Mode].Run(Force) * Levels[Mode];
        if (Floor.Grit && Index < Rate * 0.006) Out += Scrape.Run(Noise.White()) * Floor.Grit * 0.25 * Math.exp(-Index / (Rate * 0.0015));
        Out *= Strength * 0.18;
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

const Mix01 = (From, To, Amount) => From + (To - From) * Amount;

/**
 * Drops one case onto a floor at Time from Height metres. Returns the time it comes to rest. Gain is the level of the
 * first impact; Seed varies the tumble (which end lands, the clip of the other end, flight times, roll length).
 */
export function DropCasing(Left, Right, Rate, Time, ShellName, FloorName = 'Concrete', { Seed = 1, Gain = 1, Pan = 0, Height = 1.2 } = {})
{
    const Shell = Shells[ShellName], Floor = Floors[FloorName], Noise = new SeededNoise(Seed);
    const Impact = Math.sqrt(2 * Gravity * Height);
    let Speed = Impact * Noise.Range(0.85, 1.1), When = Time;
    for (let Hit = 0; Hit < 14; Hit++)
    {
        const Strength = Gain * Shell.Gain * Math.pow(Speed / Impact, 1.2);
        const Mouth = Noise.Next() < 0.5, Spot = Clamp(Pan + Noise.Gauss() * 0.03, -1, 1);
        Clink(Left, Right, Rate, When, Shell, Floor, Strength, { Pan: Spot, Seed: Seed * 31 + Hit, Where: Mouth ? Noise.Range(0.7, 1) : Noise.Range(0, 0.3) });
        // A tumbling case lands on one end and slaps down on the other a few milliseconds later.
        if (Hit < 4 && Noise.Next() < 0.65)
            Clink(Left, Right, Rate, When + Noise.Range(0.002, 0.012) * (1 + Hit), Shell, Floor, Strength * Noise.Range(0.35, 0.8), { Pan: Spot, Seed: Seed * 41 + Hit, Where: Mouth ? Noise.Range(0, 0.3) : Noise.Range(0.7, 1) });
        Strike(Left, Right, Rate, When, {
            Modes: Floor.Thud, Burst: Floor.Contact * 4, Gain: Strength * Floor.ThudGain * Shell.Mass * (Shell.Plastic ? 2 : 1) * 1.6, Pan: Spot, Seed: Seed * 37 + Hit,
            Breath: Floor.Grit > 0.5 ? { Low: 500, High: 4500, Decay: 0.04, Gain: Floor.Grit * 0.08 * Strength } : null,
        });
        const Flight = 2 * Speed / Gravity * Noise.Range(0.55, 1.15) * (Hit === 0 ? 1 : 0.9);
        Speed *= Floor.Bounce * Noise.Range(0.8, 1.2);
        When += Flight * Floor.Bounce;
        if (Speed < 0.12) break;
    }
    if (Shell.Roll && Floor.Rough) When = RollCasing(Left, Right, Rate, When, Shell, Floor, Gain * Shell.Gain, Pan, Noise);
    return When;
}

/**
 * Rolling: the case turns at speed ÷ (π × diameter) while it slows. The floor's grit scrapes the case modes with a
 * level that follows the speed (a gritty, singing whirr), the extractor rim ticks once a turn, and a tapered case
 * rolls in an arc (the pan drifts). At the end it rocks on its rim: a few quick, quieter ticks closing up.
 */
function RollCasing(Left, Right, Rate, Time, Shell, Floor, Gain, Pan, Noise)
{
    const Seconds = Shell.Roll * Floor.Rough * Noise.Range(0.6, 1.25), Start = Math.round(Time * Rate);
    const Count = Math.min(Left.length - Start, Math.round((Seconds + 0.2) * Rate));
    if (Count <= 0) return Time;
    const Modes = Shell.Plastic ? [[Shell.Hz, 0.01, 1], [Shell.Hz * 2.3, 0.008, 0.5]] : CaseModes(Shell, Noise, 0.3 * Floor.Ring);
    const Rings = Modes.map(([Hz, Decay]) => new Resonator(Rate, Hz, Decay));
    const Levels = Modes.map(([Hz, , Level], Index) => Level * Math.sin(Tau * Math.min(Hz, Rate * 0.47) / Rate) / Rings[Index].Norm);
    const Grain = new Biquad(Rate).Shape('High', 1500, 0.7), Sand = new Biquad(Rate).Shape('Band', 6000, 0.7);
    const Rolling = Noise.Range(0.3, 0.55), Drift = Noise.Range(-0.3, 0.3), Rocks = Math.round(Noise.Range(2, 5));
    const Roll = Math.round(Seconds * Rate);
    let Turn = Noise.Next(), Rock = 0, NextRock = Roll + Math.round(Noise.Range(0.03, 0.06) * Rate);
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Math.min(1, Index / Roll), Speed = Rolling * Math.pow(1 - Progress, 1.3);
        Turn += Speed / (Math.PI * Shell.Diameter) / Rate;
        let Kick = Turn >= 1 ? (Turn -= 1, Noise.Range(0.4, 1) * Speed * 0.04) : 0;
        if (Index === NextRock && Rock < Rocks) { Kick += 0.006 * Math.pow(0.55, Rock); Rock++; NextRock += Math.round(Noise.Range(0.02, 0.05) * Math.pow(0.7, Rock) * Rate); }
        // Grit: sparse sharp contacts rather than smooth hiss (each sand grain the case rides over).
        const Grit = Noise.Next() < 0.25 ? Grain.Run(Noise.White()) : Grain.Run(0);
        const Excite = Grit * Speed * 0.008 * (0.6 + 0.4 * Math.sin(Tau * Turn)) + Kick;
        let Out = 0;
        for (let Mode = 0; Mode < Rings.length; Mode++) Out += Rings[Mode].Run(Excite) * Levels[Mode];
        Out += Sand.Run(Noise.White()) * Speed * Floor.Grit * 0.04;
        Out *= Gain * 0.18 * Clamp(Index / (0.008 * Rate), 0, 1);
        const [GainL, GainR] = PanGains(Clamp(Pan + Drift * Progress, -1, 1));
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
    return Time + Seconds + 0.2;
}

/**
 * A stream of cases from a burst (Times = ejection times), landing around the shooter; later cases fall onto the pile
 * and clink off the cases already there. Belt-fed guns drop a link with every case. Returns the last rest time.
 */
export function CasingRain(Left, Right, Rate, Times, ShellName, FloorName = 'Concrete', { Seed = 1, Gain = 1, Pan = 0.5, Links = false, Fall = 0.45 } = {})
{
    const Noise = new SeededNoise(Seed), Shell = Shells[ShellName];
    let Rest = 0;
    Times.forEach((At, Index) =>
    {
        const Land = At + Fall * Noise.Range(0.8, 1.2), Spot = Clamp(Pan + Noise.Gauss() * 0.25, -1, 1);
        const Level = Gain * Noise.Range(0.6, 1);
        Rest = Math.max(Rest, DropCasing(Left, Right, Rate, Land, ShellName, FloorName, { Seed: Seed + Index * 7, Gain: Level, Pan: Spot, Height: Noise.Range(0.9, 1.4) }));
        if (Index > 4 && Noise.Next() < 0.45)                              // glances off a case already lying there
            Clink(Left, Right, Rate, Land + Noise.Range(0.003, 0.02), Shell, Floors.Metal, Level * 0.6, { Pan: Spot, Seed: Seed + Index * 13, Where: Noise.Next(), Muffle: 0.6 });
        if (Links) DropCasing(Left, Right, Rate, Land + Noise.Range(-0.05, 0.08), 'Link', FloorName, { Seed: Seed + Index * 11 + 5, Gain: Level * 0.8, Pan: Clamp(Spot - 0.3, -1, 1), Height: 0.9 });
    });
    return Rest;
}
