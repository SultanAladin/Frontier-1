// BALLISTICS
// 🧩 Synthesized weapon sounds: an energy LMG that spins up as you hold the trigger (Devotion-style, with or without the
//    Turbocharger hop-up), a belt-fed LMG, an SMG, a bolt-action sniper rifle, an automated turret, a tank cannon and an
//    autocannon; a .50 cal heavy machine gun (M2HB-style), an AK-47, an M4 carbine, an M134-style minigun, an MG 42, a
//    pump shotgun, a 9 mm pistol and a .50 anti-materiel rifle; spent casings (Casings.js); bullets passing by
//    (supersonic crack with the distant report arriving later, subsonic whizz, ricochet whine); and bullets hitting
//    sand, dirt, rock, concrete, wood, metal, glass and water.
// 📝 Follow-up 20 rebuild: every report is now built from the physics of that weapon instead of one shared chain with
//    different numbers. The muzzle blast is a Friedlander pulse whose length follows the calibre and barrel (9 mm pistol
//    0.28 ms, so a sharp high crack; 5.56 0.36 ms; 7.62×39 0.5 ms; 7.62 NATO 0.6 ms; .50 BMG 1.1 ms, a deep boom; 30 mm
//    3.5 ms), with its ground reflection, the propellant jet and a noise punch (no more pitched sine "thump", which made
//    every gun and explosion sound alike). Each gun has its own mechanism: the AK's loose, heavy carrier slam and rattle,
//    the M4's buffer spring twang, the MG 42's roller-locked buzz, the M2's recoiling barrel, the pistol's slide, the
//    minigun's motor and delinker, the energy LMG's geared motor, discharge zing and arc crackle. Every cue plays in its own
//    space by convolution (Detonation.js: open field, forest, town, valley, desert, range), then a look-ahead limiter.
//    Fire rates: FireRate(): Devotion-style 5 → 15 rounds/s over 1.75 s, 6.8 → 15 over 0.85 s with the Turbocharger.
//    Same contract as CabinFoley: Cues + RenderCue; plus RenderShot / RenderMotor for the live bench's hold-to-fire.
//    Original designs: nothing is copied or sampled from any game or recording.

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, OnePole, Biquad, Resonator, PanGains, Strike, NarrowTone, PolySaw } from './SignalKit.js';
import { Overpressure, Roar, Scatter, Whine, Echoes, Settle } from './ImpactFoley.js';
import { Shells, Floors, DropCasing, CasingRain } from './Casings.js';
import { Pulse, Rumble, Place, Limit } from './Detonation.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    WEAPONS
//------------------------------------------------------------------------------------------------------------------------

// Where the brass lands follows where the gun is heard: on a range or in a street it rings on concrete; in a field,
// a forest, a valley or the desert it drops into dirt with a dull tick (no steady case ring under the burst).
const Ground = (W) => (W.Space === 'Urban' || W.Space === 'Range' ? 'Concrete' : 'Dirt');

/**
 * Weapon recipes. Rate: [first, top] rounds per second; Spin: seconds to reach the top rate (0 = constant). Turbo
 * overrides for the hop-up.
 *   Muzzle  blast pulse: Width (positive phase s), Drive (how hard it clips), Ground ([delay s, level] reflection)
 *   Gas     propellant jet: band noise { Seconds, Hz, Q, Gain };   Punch: low noise burst { Seconds, Hz, Gain }
 *   Crack   supersonic bullet N-wave at the muzzle { Lasts, Gain } (absent for the shotgun)
 *   Ring    receiver and barrel struck by the shot { Modes, Gain }
 *   Action  mechanism events [delay s, modes, gain, burst s]: bolt or carrier back, slam home, slide, roller lock
 *   Rattle  loose parts chattering after the slam { Delay, Count, Low, High, Gain };  Spring: buffer spring twang
 *   Zap / Arc  the energy weapon's discharge zing and crackle;  Links: belt links;  Brake: muzzle brake side blasts
 *   Space   where the cue is heard (Detonation.js Spaces);  Level: live bench gain, so every weapon sits alike
 */
export const Weapons = {
    EnergyLmg: {
        // A full-power LMG report (7.62-class blast, heavy steel receiver ring, crack) carrying the Devotion's signature:
        // a geared motor whirring up as the rate climbs, a falling metallic discharge zing on every shot and a short
        // arc crackle from the energy cell; the bolt is heard as two clacks.
        Label: 'Energy LMG', Note: 'Devotion-style spin-up', Rate: [5, 15], Spin: 1.75, Magazine: 44,
        Motor: { Hz: 26, Teeth: 3, Barrels: 3, Gain: 0.07, Gear: 1 }, Turbo: { Rate: [6.8, 15], Spin: 0.85 },
        Muzzle: { Width: 0.00062, Drive: 2.9, Ground: [0.0031, 0.6] },
        Gas: { Seconds: 0.028, Hz: 1600, Q: 0.6, Gain: 0.5 }, Punch: { Seconds: 0.04, Hz: 90, Gain: 0.3 },
        Crack: { Lasts: 0.00028, Gain: 0.45 },
        Ring: { Modes: [[640, 0.08, 1], [1530, 0.06, 0.7], [2740, 0.04, 0.5], [4310, 0.025, 0.3]], Gain: 0.9 },
        Zap: { From: 3900, To: 760, Seconds: 0.06, Gain: 0.1 }, Arc: 0.035,
        Action: [[0.011, [[1050, 0.035, 1], [2300, 0.025, 0.6], [460, 0.05, 0.5]], 1.6], [0.042, [[1380, 0.03, 1], [3150, 0.02, 0.5], [620, 0.04, 0.4]], 1.1]],
        Space: 'Field', Brass: 'Rifle762', Level: 0.6,
    },
    Lmg: {
        // 7.62 NATO belt-fed LMG (M240 / FN MAG class, 640 rounds/min): open-bolt, so the bolt runs forward and fires
        // in one heavy stroke; a steady, even chug with belt links slapping the feed tray. Heard in woods.
        Label: 'LMG', Note: 'belt-fed, 640 rounds/min', Rate: [10.7, 10.7], Spin: 0, Magazine: 100,
        Muzzle: { Width: 0.0006, Drive: 2.7, Ground: [0.0028, 0.65] },
        Gas: { Seconds: 0.034, Hz: 1700, Q: 0.6, Gain: 0.55 }, Punch: { Seconds: 0.0425, Hz: 85, Gain: 0.32 },
        Crack: { Lasts: 0.00026, Gain: 0.45 },
        Ring: { Modes: [[880, 0.05, 1], [2070, 0.035, 0.6], [3450, 0.02, 0.35]], Gain: 0.6 },
        Action: [[0.003, [[720, 0.05, 1], [1640, 0.035, 0.6], [330, 0.06, 0.5]], 1.6], [0.03, [[1250, 0.03, 1], [2600, 0.02, 0.5]], 0.7]],
        Links: [[3100, 0.012, 1], [4700, 0.01, 0.6]],
        Space: 'Forest', Brass: 'Rifle762', Level: 0.65,
    },
    Smg: {
        // 9 mm SMG (900 rounds/min): a light, fast patter, short pops more than cracks (pistol bullets are near or
        // below the speed of sound), the blowback bolt chattering. Heard on a
        // covered firing range.
        Label: 'SMG', Note: '900 rounds/min', Rate: [15, 15], Spin: 0, Magazine: 30,
        Muzzle: { Width: 0.0003, Drive: 2.2, Ground: [0.0022, 0.55] },
        Gas: { Seconds: 0.014, Hz: 2600, Q: 0.7, Gain: 0.45 }, Punch: { Seconds: 0.015, Hz: 150, Gain: 0.16 },
        Ring: { Modes: [[1900, 0.03, 1], [4300, 0.02, 0.5]], Gain: 0.35 },
        Action: [[0.006, [[2100, 0.02, 1], [3900, 0.012, 0.6], [950, 0.025, 0.5]], 1.4], [0.028, [[1750, 0.02, 1], [3400, 0.012, 0.5]], 1.0]],
        Space: 'Range', Brass: 'Pistol', Level: 0.85,
    },
    Turret: {
        Label: 'Turret', Note: 'automated, 720 rounds/min', Rate: [12, 12], Spin: 0, Magazine: 200,
        Motor: { Hz: 38, Teeth: 1, Barrels: 0, Gain: 0.05, Gear: 0 },
        Muzzle: { Width: 0.00052, Drive: 2.6, Ground: [0.0035, 0.5] },
        Gas: { Seconds: 0.026, Hz: 1900, Q: 0.6, Gain: 0.5 }, Punch: { Seconds: 0.035, Hz: 95, Gain: 0.26 },
        Crack: { Lasts: 0.00025, Gain: 0.4 },
        Ring: { Modes: [[1180, 0.06, 1], [2650, 0.04, 0.6], [4950, 0.02, 0.3]], Gain: 0.6 },
        Action: [[0.008, [[1500, 0.03, 1], [2900, 0.02, 0.6], [620, 0.04, 0.5]], 1.3]],
        Links: [[3600, 0.01, 1], [5400, 0.008, 0.5]],
        Space: 'Urban', Brass: 'Rifle762', Level: 0.7,
    },
    AutoCannon: {
        // 30 mm (210 rounds/min): each round a separate, chest-deep bang, the breech block slamming like a hammer.
        Label: 'Autocannon', Note: '30 mm, 210 rounds/min', Rate: [3.5, 3.5], Spin: 0, Magazine: 40,
        Muzzle: { Width: 0.0035, Drive: 2.4, Ground: [0.006, 0.6] },
        Gas: { Seconds: 0.1, Hz: 600, Q: 0.6, Gain: 0.6 }, Punch: { Seconds: 0.14, Hz: 48, Gain: 0.42 },
        Crack: { Lasts: 0.0009, Gain: 0.4 },
        Ring: { Modes: [[260, 0.25, 1], [610, 0.16, 0.6], [1350, 0.08, 0.35]], Gain: 0.6 },
        Action: [[0.05, [[420, 0.08, 1], [980, 0.05, 0.7], [2200, 0.03, 0.4]], 2.4, 0.0015]],
        Space: 'Valley', Brass: 'Cannon30', Level: 0.45,
    },

    // ── Real-world guns (published cyclic rates and calibres; voicing is original, not sampled) ──────────────────────
    Fifty: {
        // M2HB-style .50 BMG (550 rounds/min): each round a slow, deep chug: a long 1.1 ms blast (energy down near
        // 300 Hz), a wide crack, then the barrel and bolt recoiling together and slamming back: "chunk".
        Label: '.50 cal HMG', Note: 'M2-style, 550 rounds/min', Rate: [9.2, 9.2], Spin: 0, Magazine: 100,
        Muzzle: { Width: 0.0011, Drive: 2.6, Ground: [0.0042, 0.6] },
        Gas: { Seconds: 0.055, Hz: 950, Q: 0.6, Gain: 0.6 }, Punch: { Seconds: 0.07, Hz: 62, Gain: 0.4 },
        Crack: { Lasts: 0.00042, Gain: 0.5 },
        Ring: { Modes: [[410, 0.12, 1], [980, 0.08, 0.6], [1870, 0.05, 0.4], [3100, 0.03, 0.2]], Gain: 0.7 },
        Action: [[0.024, [[520, 0.07, 1], [1180, 0.05, 0.6], [260, 0.09, 0.6], [2300, 0.025, 0.3]], 2.6, 0.0012], [0.07, [[690, 0.05, 1], [1600, 0.035, 0.5]], 1.4]],
        Links: [[2700, 0.014, 1], [4300, 0.011, 0.6]],
        Space: 'Valley', Brass: 'Fifty', Level: 0.5,
    },
    Ak47: {
        // 7.62×39 assault rifle (600 rounds/min): a mid-heavy, harsh report (0.5 ms blast, energy near 700 Hz) and the
        // AK's signature hardware: a stamped steel receiver that rings, a heavy, loosely fitted carrier slamming back and
        // forward, and the parts rattling after it. The cue is heard between buildings.
        Label: 'AK-47', Note: 'assault rifle, 600 rounds/min', Rate: [10, 10], Spin: 0, Magazine: 30,
        Muzzle: { Width: 0.0005, Drive: 3.0, Ground: [0.0026, 0.65] },
        Gas: { Seconds: 0.026, Hz: 1900, Q: 0.55, Gain: 0.6 }, Punch: { Seconds: 0.03, Hz: 105, Gain: 0.26 },
        Crack: { Lasts: 0.00022, Gain: 0.4 },
        Ring: { Modes: [[1240, 0.05, 1], [2930, 0.035, 0.7], [5150, 0.018, 0.4], [760, 0.04, 0.4]], Gain: 0.9 },
        Action: [[0.013, [[860, 0.04, 1], [1950, 0.03, 0.7], [400, 0.05, 0.5], [3300, 0.015, 0.3]], 2.4], [0.056, [[980, 0.04, 1], [2250, 0.03, 0.6], [470, 0.05, 0.5]], 2.0]],
        Rattle: { Delay: 0.062, Count: 3, Low: 1800, High: 4200, Gain: 0.6 },
        Space: 'Urban', Brass: 'Rifle762', Level: 0.8,
    },
    M4: {
        // 5.56 carbine (about 800 rounds/min): a brighter, snappier crack (0.36 ms blast, energy near 1 kHz), an
        // aluminium receiver's dry tick, and the M4's tell: the buffer spring's metallic "sproing" behind every shot.
        Label: 'M4 carbine', Note: '5.56 mm, 800 rounds/min', Rate: [13.3, 13.3], Spin: 0, Magazine: 30,
        Muzzle: { Width: 0.00036, Drive: 3.2, Ground: [0.0024, 0.6] },
        Gas: { Seconds: 0.018, Hz: 3000, Q: 0.6, Gain: 0.5 }, Punch: { Seconds: 0.0225, Hz: 120, Gain: 0.21 },
        Crack: { Lasts: 0.00018, Gain: 0.45 },
        Ring: { Modes: [[2150, 0.02, 1], [4600, 0.014, 0.6]], Gain: 0.4 },
        Action: [[0.008, [[1500, 0.025, 1], [3200, 0.018, 0.6], [700, 0.03, 0.4]], 1.3], [0.04, [[1800, 0.022, 1], [3800, 0.015, 0.5]], 1.0]],
        Spring: { Delay: 0.012, Hz: 560, Decay: 0.16, Gain: 0.16 },
        Space: 'Field', Brass: 'Rifle556', Level: 0.85,
    },
    Minigun: {
        // M134-style six-barrel rotary gun: an electric drive spins the barrels up in about 0.5 s to 3000 rounds/min
        // (50 a second), so the shots merge into one tearing "brrrt" over the motor and the delinker. Out in the desert.
        Label: 'Minigun', Note: 'M134-style, 3000 rounds/min', Rate: [16, 50], Spin: 0.5, Magazine: 1500,
        Motor: { Hz: 8, Teeth: 6, Barrels: 1, Gain: 0.07, Gear: 0.6 },
        Muzzle: { Width: 0.00058, Drive: 2.4, Ground: [0.0034, 0.5] },
        Gas: { Seconds: 0.02, Hz: 1700, Q: 0.6, Gain: 0.4 }, Punch: { Seconds: 0.02, Hz: 95, Gain: 0.19 },
        Crack: { Lasts: 0.00026, Gain: 0.35 },
        Ring: { Modes: [[1450, 0.03, 1], [3300, 0.02, 0.5]], Gain: 0.3 },
        Action: [[0.004, [[1600, 0.015, 1], [3400, 0.01, 0.5]], 0.6]], Links: [[3100, 0.008, 1], [4700, 0.006, 0.6]],
        Space: 'Desert', Brass: 'Rifle762', Level: 0.45,
    },
    Mg42: {
        // MG 42: 1200 rounds/min, a report every 50 ms, faster than the ear can count: the ripping "buzzsaw". The
        // roller-locked bolt adds a sharp tick to every shot, the light receiver rings high. Heard in woods.
        Label: 'MG 42', Note: '1200 rounds/min buzzsaw', Rate: [20, 20], Spin: 0, Magazine: 50,
        Muzzle: { Width: 0.00056, Drive: 3.3, Ground: [0.003, 0.65] },
        Gas: { Seconds: 0.022, Hz: 1800, Q: 0.6, Gain: 0.5 }, Punch: { Seconds: 0.025, Hz: 92, Gain: 0.24 },
        Crack: { Lasts: 0.00025, Gain: 0.45 },
        Ring: { Modes: [[1720, 0.035, 1], [3860, 0.025, 0.6], [6100, 0.012, 0.3]], Gain: 0.7 },
        Action: [[0.004, [[2300, 0.015, 1], [4700, 0.01, 0.5], [1100, 0.02, 0.4]], 1.2], [0.022, [[1500, 0.018, 1], [3200, 0.012, 0.5]], 0.8]],
        Links: [[3300, 0.01, 1], [5000, 0.008, 0.6]],
        Space: 'Forest', Brass: 'Rifle762', Level: 0.7,
    },
    Shotgun: {
        // 12-gauge pump: the widest bore here, so a big, round 0.9 ms boom and no bullet crack; then the pump: fore-end
        // back (the hull flies out), fore-end forward.
        Label: 'Pump shotgun', Note: '12 gauge, pump action', Rate: [1.4, 1.4], Spin: 0, Magazine: 6,
        Muzzle: { Width: 0.0009, Drive: 2.2, Ground: [0.003, 0.6] },
        Gas: { Seconds: 0.045, Hz: 1100, Q: 0.5, Gain: 0.65 }, Punch: { Seconds: 0.06, Hz: 95, Gain: 0.39 },
        Ring: { Modes: [[520, 0.07, 1], [1310, 0.05, 0.5]], Gain: 0.4 },
        Pump: 0.36, Action: [[0.002, [[1800, 0.01, 1]], 0.4]],
        Space: 'Field', Brass: 'Shotgun', Level: 0.6,
    },
    Pistol: {
        // 9 mm semi-automatic: the shortest pulse (0.28 ms, energy above 1 kHz) so a sharp, high "pak", the slide
        // slamming back and forward. Heard in an alley (Urban), where the walls slap it back fast.
        Label: '9 mm pistol', Note: 'semi-auto, 17 rounds', Rate: [4, 4], Spin: 0, Magazine: 17,
        Muzzle: { Width: 0.00028, Drive: 2.6, Ground: [0.002, 0.55] },
        Gas: { Seconds: 0.012, Hz: 2800, Q: 0.7, Gain: 0.45 }, Punch: { Seconds: 0.014, Hz: 160, Gain: 0.16 },
        Ring: { Modes: [[2400, 0.02, 1], [5200, 0.012, 0.5]], Gain: 0.3 },
        Action: [[0.005, [[1900, 0.02, 1], [3600, 0.012, 0.6], [850, 0.025, 0.5]], 1.4], [0.03, [[2300, 0.018, 1], [4500, 0.012, 0.5], [1100, 0.02, 0.4]], 1.6]],
        Space: 'Urban', Brass: 'Pistol', Level: 1,
    },
    AntiMateriel: {
        // .50 BMG anti-materiel rifle: the muzzle brake throws most of the gas sideways in two extra blasts, so it is the
        // loudest, widest shot here; a 1.3 ms blast, a wide crack, the recoiling barrel slamming home; a valley answers.
        Label: '.50 rifle', Note: 'anti-materiel, muzzle brake', Rate: [1.2, 1.2], Spin: 0, Magazine: 10,
        Muzzle: { Width: 0.0013, Drive: 2.8, Ground: [0.0045, 0.6] },
        Gas: { Seconds: 0.07, Hz: 850, Q: 0.6, Gain: 0.7 }, Punch: { Seconds: 0.09, Hz: 58, Gain: 0.42 },
        Crack: { Lasts: 0.00045, Gain: 0.55 }, Brake: { Gain: 0.85 },
        Ring: { Modes: [[380, 0.12, 1], [910, 0.08, 0.6], [1750, 0.05, 0.3]], Gain: 0.6 },
        Action: [[0.03, [[700, 0.06, 1], [1500, 0.04, 0.6], [320, 0.08, 0.5]], 2.2, 0.001]],
        Space: 'Valley', Brass: 'Fifty', Level: 0.45,
    },
};

/** Rounds per second after the trigger has been held for Held seconds (linear spin-up to the top rate). */
export function FireRate(Name, Held, Turbo = false)
{
    const Weapon = Weapons[Name], Spec = Turbo && Weapon.Turbo ? { ...Weapon, ...Weapon.Turbo } : Weapon;
    const [First, Top] = Spec.Rate;
    return Spec.Spin ? First + (Top - First) * Clamp(Held / Spec.Spin, 0, 1) : First;
}

/**
 * Buffer spring twang (AR-15 family): the long coil spring is hit by the buffer and rings with a slightly falling,
 * dispersive pitch (coil springs are inharmonic), a metallic "sproing" behind the report.
 */
function Twang(Left, Right, Rate, Time, { Hz = 560, Decay = 0.16, Gain = 0.15, Pan = 0, Seed = 1 } = {})
{
    const Start = Math.round(Time * Rate), Count = Math.min(Left.length - Start, Math.round(Decay * Rate));
    if (Count <= 0) return;
    const Noise = new SeededNoise(Seed), Partials = [1, 2.42, 4.13, 6.3], [GainL, GainR] = PanGains(Pan);
    const Rings = Partials.map((Ratio) => new Resonator(Rate, Hz * Ratio, Decay / Math.sqrt(Ratio)));
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Index / Count;
        if ((Index & 31) === 0) Rings.forEach((Ring, Part) => Ring.Tune(Hz * Partials[Part] * (1 - 0.06 * Progress) * (1 + 0.01 * Part * Progress), Decay / Math.sqrt(Partials[Part])));
        const Excite = Index < Rate * 0.0012 ? Noise.White() : 0;
        let Out = 0;
        for (let Part = 0; Part < Rings.length; Part++) Out += Rings[Part].Run(Excite) / Rings[Part].Norm * [1, 0.6, 0.35, 0.2][Part];
        Out *= Gain * 0.02;
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/**
 * The energy weapon's discharge: a metallic zing falling fast from From to To Hz (two inharmonic partials of a struck
 * bar, plus a band of noise riding them) and a short arc crackle (sparse sharp pops). Short and low in the mix: it
 * colours the report rather than replacing it.
 */
function Discharge(Left, Right, Rate, Time, { From = 3900, To = 760, Seconds = 0.06, Gain = 0.2, Arc = 0.1, Pan = 0, Seed = 1 } = {})
{
    const Start = Math.round(Time * Rate), Count = Math.min(Left.length - Start, Math.round(Seconds * 1.6 * Rate));
    if (Count <= 0) return;
    const Noise = new SeededNoise(Seed), Tones = [new NarrowTone(Rate, { Breath: 0.35, Width: 0.04, Jitter: 0.01, Flutter: 0.3, Seed }), new NarrowTone(Rate, { Breath: 0.5, Width: 0.05, Jitter: 0.01, Flutter: 0.3, Seed: Seed + 3 })];
    const Spark = new Biquad(Rate).Shape('High', 3500, 0.7), Pop = new Resonator(Rate, 6200, 0.002), [GainL, GainR] = PanGains(Pan);
    for (let Index = 0; Index < Count; Index++)
    {
        const T = Index / Rate, Hz = To + (From - To) * Math.exp(-T / (Seconds * 0.35));
        const Envelope = Math.min(1, T / 0.0015) * Math.exp(-T / (Seconds * 0.4));
        let Out = (Tones[0].Run(Hz) + Tones[1].Run(Hz * 2.76) * 0.45) * Envelope * Gain;
        const Kick = Noise.Next() < 0.004 * Math.exp(-T / 0.02) ? Noise.Range(0.5, 1) * (Noise.Next() < 0.5 ? -1 : 1) : 0;
        Out += (Spark.Run(Pop.Run(Kick) / Pop.Norm * 0.08) + Spark.Run(Noise.White()) * 0.02 * Math.exp(-T / 0.008)) * Arc * 6;
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/** Draws one shot of a weapon at Time, dry (callers put the whole cue in its space once). */
function DrawShot(Left, Right, Rate, Time, Name, Seed, { Pan = 0, Gain = 1, Brass = true } = {})
{
    const W = Weapons[Name], Noise = new SeededNoise(Seed), Vary = (Spread = 0.05) => 1 + Noise.Gauss() * Spread;
    Pulse(Left, Right, Rate, Time, { ...W.Muzzle, Gain: Gain, Gas: W.Gas, Low: W.Punch, Pan, Seed, Vary: 0.06 });
    if (W.Crack) Snap(Left, Right, Rate, Time + 0.00015, { Lasts: W.Crack.Lasts * Vary(), Gain: W.Crack.Gain * Gain, Pan });
    if (W.Ring) Strike(Left, Right, Rate, Time, { Modes: W.Ring.Modes.map(([Hz, Decay, Part]) => [Hz * Vary(0.004), Decay * Vary(), Part]), Burst: 0.0005, Gain: W.Ring.Gain * Gain * Vary(0.1), Pan, Seed: Seed + 2 });
    (W.Action ?? []).forEach(([Delay, Modes, Level, Burst = 0.0007], Step) =>
        Strike(Left, Right, Rate, Time + Delay * Vary(0.08), { Modes: Modes.map(([Hz, Decay, Part]) => [Hz * Vary(0.02), Decay, Part]), Burst, Gain: Level * Gain * Vary(0.12), Pan: Pan + 0.12, Seed: Seed + 10 + Step }));
    if (W.Rattle)
    {
        const R = W.Rattle;
        let At = Time + R.Delay;
        for (let Hit = 0; Hit < R.Count; Hit++)
        {
            At += Noise.Range(0.003, 0.011);
            Strike(Left, Right, Rate, At, { Modes: [[Noise.Range(R.Low, R.High), 0.012, 1], [Noise.Range(R.High, R.High * 1.8), 0.006, 0.5]], Burst: 0.0003, Gain: R.Gain * Gain * Noise.Range(0.4, 1) * Math.pow(0.7, Hit), Pan: Pan + 0.15, Seed: Seed + 30 + Hit });
        }
    }
    if (W.Spring) Twang(Left, Right, Rate, Time + W.Spring.Delay, { Hz: W.Spring.Hz * Vary(0.01), Decay: W.Spring.Decay, Gain: W.Spring.Gain * Gain, Pan: Pan + 0.1, Seed: Seed + 4 });
    if (W.Links) Strike(Left, Right, Rate, Time + 0.03 * Vary(0.1), { Modes: W.Links, Burst: 0.0004, Gain: 1.4 * Gain * Vary(0.15), Pan: Pan + 0.25, Seed: Seed + 3 });
    if (W.Zap) Discharge(Left, Right, Rate, Time + 0.0008, { ...W.Zap, From: W.Zap.From * Vary(0.03), Arc: W.Arc ?? 0, Gain: W.Zap.Gain * Gain, Pan, Seed: Seed + 5 });
    if (W.Brake)
        for (const Side of [-0.85, 0.85])
            Pulse(Left, Right, Rate, Time + 0.0004, { Width: W.Muzzle.Width * 0.8, Gain: W.Brake.Gain * Gain, Drive: 2.4, Ground: [0.0035, 0.5], Gas: { Seconds: 0.03, Hz: 1300, Q: 0.6, Gain: 0.35 }, Pan: Side, Seed: Seed + (Side > 0 ? 7 : 8) });
    if (W.Pump) { PumpCycle(Left, Right, Rate, Time + W.Pump, { Pan: Pan + 0.25, Gain, Seed: Seed + 8 }); if (Brass && W.Brass) DropCasing(Left, Right, Rate, Time + W.Pump + Noise.Range(0.38, 0.46), W.Brass, Ground(W), { Seed: Seed + 5, Gain: 0.4 * Gain, Pan: Pan + 0.6, Height: 1.1 }); return; }
    if (Brass && W.Brass) DropCasing(Left, Right, Rate, Time + Noise.Range(0.32, 0.5), W.Brass, Ground(W), { Seed: Seed + 5, Gain: 0.4 * Gain, Pan: Pan + Noise.Range(0.3, 0.7), Height: Noise.Range(1.0, 1.3) });
}

/**
 * Pump action: the fore-end dragged back (wood on steel), the action unlocking with a hard clack, the hull kicked out,
 * then the fore-end slammed forward chambering the next shell (two clacks close together).
 */
function PumpCycle(Left, Right, Rate, Time, { Pan = 0, Gain = 1, Seed = 600 } = {})
{
    Roar(Left, Right, Rate, Time, 0.07, { From: 4500, To: 2500, Floor: 900, Gain: 0.1 * Gain, Attack: 0.01, Curve: 0.8, Grit: 0.7, Seed });
    Strike(Left, Right, Rate, Time + 0.03, { Modes: [[1250, 0.035, 1], [2700, 0.022, 0.7], [560, 0.04, 0.5], [4300, 0.012, 0.3]], Burst: 0.0008, Gain: 3.2 * Gain, Pan, Seed: Seed + 1 });
    Roar(Left, Right, Rate, Time + 0.16, 0.06, { From: 5000, To: 2800, Floor: 900, Gain: 0.09 * Gain, Attack: 0.008, Curve: 0.8, Grit: 0.7, Seed: Seed + 2 });
    Strike(Left, Right, Rate, Time + 0.21, { Modes: [[1450, 0.03, 1], [3100, 0.02, 0.6], [640, 0.035, 0.5]], Burst: 0.0008, Gain: 3.4 * Gain, Pan, Seed: Seed + 3 });
    Strike(Left, Right, Rate, Time + 0.225, { Modes: [[1900, 0.025, 1], [4000, 0.015, 0.5]], Burst: 0.0005, Gain: 2 * Gain, Pan, Seed: Seed + 4 });
}

/**
 * A spin drive (energy LMG, minigun, turret servo) as machinery, not a tone: the armature's commutator buzz (a
 * band-limited saw through the housing's two resonances), the gear mesh as narrow noise bands (NarrowTone) at Teeth ×
 * shaft rate and twice that, the rotating barrels' air swish, and barrel clatter Barrels × per round. Gear 0 = a servo
 * (buzz only), 1 = a fully geared drive. Loop: no pitch drift (the bench loop must not wander from its playback rate).
 */
class SpinDrive
{
    constructor(Rate, Motor, Seed, { Loop = false, Turbo = false } = {})
    {
        this.Rate = Rate; this.Motor = Motor; this.Noise = new SeededNoise(Seed); this.Turbo = Turbo;
        const Jitter = Loop ? 0 : 0.004;
        this.Saw = new PolySaw();
        this.Body = [new Biquad(Rate).Shape('Band', 820, 2.2), new Biquad(Rate).Shape('Band', 2350, 2.8), new Biquad(Rate).Shape('Low', 5000, 0.7)];
        this.Mesh = new NarrowTone(Rate, { Breath: 0.5, Width: 0.03, Jitter, Flutter: 0.25, Seed });
        this.Mesh2 = new NarrowTone(Rate, { Breath: 0.7, Width: 0.05, Jitter, Flutter: 0.3, Seed: Seed + 5 });
        this.Swish = new Biquad(Rate).Shape('Band', 1400, 0.8);
        this.Clatter = [new Resonator(Rate, 2600, 0.012), new Resonator(Rate, 4100, 0.009), new Resonator(Rate, 1250, 0.02)];
        this.Blades = new NarrowTone(Rate, { Breath: 0.6, Width: 0.035, Jitter, Flutter: 0.35, Seed: Seed + 9 });
        this.Blades2 = new NarrowTone(Rate, { Breath: 0.8, Width: 0.05, Jitter, Flutter: 0.35, Seed: Seed + 11 });
        this.Rush = new Biquad(Rate).Shape('Band', 5200, 0.9);
        this.Turn = 0;
    }

    /** One sample at Spin rounds per second (Share = Spin / top rate, for the air and the turbine). */
    Run(Spin, Share)
    {
        const M = this.Motor, Shaft = Spin * M.Hz, Noise = this.Noise;
        const Buzz = this.Saw.Run(Shaft * 2, this.Rate);
        let Out = (this.Body[0].Run(Buzz) * 0.9 + this.Body[1].Run(Buzz) * 0.6 + this.Body[2].Run(Buzz) * 0.15) * (1 - M.Gear * 0.4);
        if (M.Gear) Out += (this.Mesh.Run(Shaft * M.Teeth) * 0.45 + this.Mesh2.Run(Shaft * M.Teeth * 2) * 0.22) * M.Gear;
        Out += this.Swish.Run(Noise.White()) * 0.5 * Share * Share;
        if (M.Barrels)
        {
            this.Turn += Spin * M.Barrels / this.Rate;
            const Kick = this.Turn >= 1 ? (this.Turn -= 1, Noise.Range(0.6, 1)) : 0;
            Out += (this.Clatter[0].Run(Kick) + this.Clatter[1].Run(Kick) * 0.6 + this.Clatter[2].Run(Kick) * 0.5) / this.Clatter[0].Norm * 0.12;
        }
        if (this.Turbo)
        {
            // Turbocharger hop-up: a small turbine on the drive, its blade-pass pitch riding up with the spin (a
            // breathy narrow band, not a sine) over the air it moves.
            const Pitch = 3000 + Spin * 380;
            Out += (this.Blades.Run(Pitch) * 0.2 + this.Blades2.Run(Pitch * 2) * 0.07 + this.Rush.Run(Noise.White()) * 0.25) * Smoothstep(0.1, 0.7, Share);
        }
        return Out;
    }
}

/**
 * The spin drive of the energy LMG, minigun and turret during a burst: its pitch tracks the fire rate, a short pre-spin
 * on the trigger pull, a spin-down after release; with the Turbocharger a pressurising hiss on the pull.
 */
function DrawMotor(Left, Right, Rate, Start, Stop, Name, Turbo)
{
    const W = Weapons[Name], Motor = W.Motor, Top = FireRate(Name, 99, Turbo);
    const From = Math.max(0, Start - 0.08), First = Math.round(From * Rate), Count = Math.min(Left.length, Math.round((Stop + 1.2) * Rate)) - First;
    const Drive = new SpinDrive(Rate, Motor, 96, { Turbo: Turbo && !!W.Turbo });
    for (let Index = 0; Index < Count; Index++)
    {
        const Time = From + Index / Rate, Firing = Time < Stop;
        let Spin = Time < Start ? (Time - From) / (Start - From || 1) * FireRate(Name, 0, Turbo) : FireRate(Name, Time - Start, Turbo);
        if (!Firing) Spin = FireRate(Name, Stop - Start, Turbo) * Math.exp(-(Time - Stop) / 0.34);
        const Level = Motor.Gain * Smoothstep(0, 0.06, Time - From) * (Firing ? 1 : Math.exp(-(Time - Stop) / 0.5));
        const Out = Drive.Run(Spin, Spin / Top) * Level;
        Left[First + Index] += Out; Right[First + Index] += Out * 0.92;
    }
    if (Turbo && W.Turbo) Roar(Left, Right, Rate, From, 0.3, { From: 3000, To: 9000, Floor: 1500, Gain: 0.1, Attack: 0.01, Curve: 1, Seed: 98, Shape: (P) => Math.sin(Math.PI * P) });
}

/**
 * Holds the trigger for Seconds (or until the magazine empties). Returns the shot times. Brass false skips the cases
 * (distant gunfire in WarFoley.js, where they would be inaudible).
 */
export function Burst(Left, Right, Rate, Start, Name, { Seconds = 3, Turbo = false, Seed = 300, Pan = 0, Brass = true } = {})
{
    const W = Weapons[Name], Times = [];
    let Held = 0;
    while (Held < Seconds && Times.length < W.Magazine) { Times.push(Held); Held += 1 / FireRate(Name, Held, Turbo); }
    Times.forEach((At, Index) => DrawShot(Left, Right, Rate, Start + At, Name, Seed + Index * 11, { Pan, Brass }));
    const Stop = Start + (Times.at(-1) ?? 0) + 0.04;
    if (W.Motor) DrawMotor(Left, Right, Rate, Start, Stop, Name, Turbo);
    if (Times.length >= W.Magazine) Strike(Left, Right, Rate, Stop + 0.06, { Modes: [[2400, 0.02, 1], [4100, 0.012, 0.5]], Burst: 0.0004, Gain: 1.5, Pan, Seed: Seed + 999 });   // empty: dry click
    return { Times, Stop };
}

/** Puts a finished weapon cue in its space, then limits it the way a recorded, mastered gun sound is (Detonation.js). */
export function Finale(Left, Right, Rate, Space = 'Field', { Wet = 1, Drive = 2.6, Seed = 1 } = {})
{
    Place(Left, Right, Rate, Space, { Wet, Seed });
    // A gentle 40 Hz high-pass first: sub-bass would only make the limiter pump without adding weight you can hear.
    const Floors = [new Biquad(Rate).Shape('High', 40, 0.6), new Biquad(Rate).Shape('High', 40, 0.6)];
    for (let Index = 0; Index < Left.length; Index++) { Left[Index] = Floors[0].Run(Left[Index]); Right[Index] = Floors[1].Run(Right[Index]); }
    Settle(Left, Right, Rate);
    Limit(Left, Right, Rate, { Drive });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BULLET PIECES
//------------------------------------------------------------------------------------------------------------------------

/** Supersonic bullet N-wave: a sudden compression, a linear fall through suction and a sudden return, Lasts long. */
export function Snap(Left, Right, Rate, Time, { Lasts = 0.0003, Gain = 1, Pan = 0 } = {})
{
    const Start = Math.round(Time * Rate), Length = Math.max(3, Math.round(Lasts * Rate)), [GainL, GainR] = PanGains(Pan);
    const Soften = new OnePole(Rate, 15000);
    for (let Index = 0; Index < Length + 24 && Start + Index < Left.length; Index++)
    {
        const Wave = Index < Length ? 1 - 2 * Index / Length : 0;
        const Out = Soften.Run(Wave) * Gain;
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/** Air bubble (water impacts): a decaying sine whose pitch rises as the bubble rises and shrinks. */
function Bubble(Left, Right, Rate, Time, { Hz = 900, Rise = 0.8, Decay = 0.06, Gain = 0.3, Pan = 0 } = {})
{
    const Start = Math.round(Time * Rate), Count = Math.min(Left.length - Start, Math.round(Decay * Rate)), [GainL, GainR] = PanGains(Pan);
    let Phase = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Index / Count;
        Phase += Hz * (1 + Rise * Progress) / Rate;
        const Out = Math.sin(Tau * Phase) * Math.exp(-Progress * 6.9) * Math.min(1, Index / (Rate * 0.0008)) * Gain;
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/**
 * Ricochet (Follow-up 21). A glancing round leaves the surface as a deformed, tumbling fragment. Its whine is not a
 * clean falling sine: the fragment sheds a narrow band of turbulent noise (pitch roughly proportional to its speed)
 * and every half-turn of its tumble swings an edge into the airflow, so the band is chopped and frequency-wobbled at the
 * tumble rate (25–70 Hz): the wavering, ragged "pt-wheeeoww". It decelerates hard (quadratic drag), so pitch and tumble
 * both sag, and it really moves: the listener hears it through geometry. Emission time is stepped on the listener's
 * clock (dt_emit / dt_heard = 1 / (1 + radial speed / c)), so the Doppler drop, the 1 / range fade and air absorption
 * all come from the flight path. Toward > 0 aims the fragment that many metres past the listener (a zing by the ear);
 * otherwise it flies off into the distance. A second, smaller fragment sometimes peels off higher and shorter.
 */
function Ricochet(Left, Right, Rate, Time, { Hz = 6000, Tumble = 45, Speed = 480, Seconds = 1.1, Gain = 0.25, Pan = 0, Away = 0.8, Toward = 0, Range = 18, Seed = 71, Spare = true } = {})
{
    const Noise = new SeededNoise(Seed), Sound = 343, Drag = 0.012;
    const Start = Math.round(Time * Rate), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    if (Count <= 0) return;
    // Listener at the origin, facing +y. The strike is Range metres off, at the side the pan says.
    const Strike0 = [Pan * Range * 0.8, Range * Math.sqrt(1 - 0.64 * Pan * Pan), 0.5];
    let Aim;
    if (Toward > 0) Aim = [Toward * Math.sign(Away || 1) - Strike0[0], -Strike0[1] - 6, 1.5 - Strike0[2]];      // passes beside the head
    else Aim = [Away * Range * 2 - Strike0[0] * 0.3, Range * 2.5, Range * 0.4];                            // off and away
    const Length = Math.hypot(...Aim), Way = Aim.map((Part) => Part / Length);
    const Where = (Emit) => { const Run = Math.log(1 + Drag * Speed * Emit) / Drag; return Strike0.map((Part, Axis) => Part + Way[Axis] * Run); };
    const SpeedAt = (Emit) => Speed / (1 + Drag * Speed * Emit);
    const Tone = new NarrowTone(Rate, { Breath: 0.3, Width: 0.015, Jitter: 0.006, Flutter: 0.2, Seed: Seed + 1 });
    const Upper = new NarrowTone(Rate, { Breath: 0.7, Width: 0.04, Jitter: 0.012, Flutter: 0.35, Seed: Seed + 2 });
    const Hiss = new Biquad(Rate), Air = [new OnePole(Rate, 12000), new OnePole(Rate, 12000)], Floor = new Biquad(Rate).Shape('High', 400, 0.7);
    const First = Math.hypot(...Strike0);
    let Emit = 0, Spin = Noise.Next(), Wobble = 1, Fade = 1;
    for (let Index = 0; Index < Count; Index++)
    {
        const Spot = Where(Emit), Range1 = Math.hypot(...Spot), Velocity = SpeedAt(Emit);
        const Radial = Way.reduce((Sum, Part, Axis) => Sum + Part * Velocity * Spot[Axis], 0) / Range1;      // + receding
        const Step = 1 / (1 + Radial / Sound) / Rate;
        Emit += Step;
        const Pitch = Hz * Math.pow(Velocity / Speed, 0.85), Turn = Tumble * Math.pow(Velocity / Speed, 0.7);
        Spin += Turn * Step * (1 + 0.01 * Noise.Gauss());
        if (Spin >= 1) { Spin -= 1; Wobble = Noise.Range(0.6, 1.25); }                          // each turn a little different
        const Edge = Math.pow(Math.abs(Math.sin(Math.PI * Spin)), 1.2);                           // edge into the flow once a turn
        const Heard = Pitch * Step * Rate * (1 + 0.035 * Math.sin(Tau * Spin));
        if ((Index & 31) === 0)
        {
            Hiss.Shape('Band', Clamp(Heard * 1.6, 500, 15000), 1.2);
            const Cut = Clamp(15000 * Math.pow(20 / Range1, 0.8), 1800, 16000); Air[0].Set(Cut); Air[1].Set(Cut);
        }
        const Source = (Tone.Run(Heard) * 0.9 + Upper.Run(Heard * 2.04) * 0.2) * (0.45 + 0.55 * Edge * Wobble) + Hiss.Run(Noise.White()) * (0.25 + 0.25 * Edge);
        Fade = Smoothstep(0, 0.012, Index / Rate) * (1 - Smoothstep(0.82, 1, Index / Count));
        const Level = Clamp(First / Range1, 0, 4) * Fade * Gain;
        const Side = Clamp(Spot[0] / Math.max(1, Math.hypot(Spot[0], Spot[1])), -1, 1);
        const [GainL, GainR] = PanGains(Side * 0.9);
        const Out = Floor.Run(Source) * Level;
        Left[Start + Index] += Air[0].Run(Out * GainL); Right[Start + Index] += Air[1].Run(Out * GainR);
    }
    if (Spare && Noise.Next() < 0.7)                                                             // a smaller chip, higher and quicker
        Ricochet(Left, Right, Rate, Time + Noise.Range(0.002, 0.01), { Hz: Hz * Noise.Range(1.35, 1.7), Tumble: Tumble * Noise.Range(1.3, 1.8), Speed: Speed * 0.8, Seconds: Seconds * 0.45, Gain: Gain * 0.35, Pan, Away: -Away * 0.6, Range, Seed: Seed + 9, Spare: false });
}

/**
 * Subsonic whizz (Follow-up 22): a round slower than sound (about 300 m/s: 9 mm, .45, suppressed .300) passing
 * Miss metres from the ear on a straight line. It sheds a turbulent wake: a breathy band near the vortex-shedding
 * pitch (Hz, set by speed over calibre, about 5–8 kHz) over broadband rush. Emission time is solved per sample
 * (heard = emitted + range / c), so the geometry does the rest: everything the round makes on the way in arrives
 * squeezed into a few milliseconds (a sudden, very high "zz"), then the departing wake is stretched and dropped
 * nearly an octave (the falling "-iuu"), and the wake is louder behind the round than ahead. Pan → EndPan sets the
 * direction (left to right or back); From (legacy) sets the shedding pitch at about twice the old start pitch.
 */
export function Whizz(Left, Right, Rate, Time, { From = 3200, Hz = From * 2, Speed = 300, Miss = 1.6, Seconds = 0.4, Gain = 0.4, Pan = -1, EndPan = 1, Seed = 81 } = {})
{
    const Start = Math.round(Time * Rate), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    if (Count <= 0) return;
    const Sound = 343, Way = Math.sign(EndPan - Pan) || 1, Rise = 0.4;
    const Noise = new SeededNoise(Seed), Band = new Biquad(Rate), Edge = new Biquad(Rate), Rush = new Biquad(Rate).Shape('High', 1200, 0.6), Air = new OnePole(Rate, 14000), Floor = new Biquad(Rate).Shape('High', 350, 0.7);
    const Heard = Seconds / 2;                                              // closest approach is heard mid-window
    let Emit = Heard - Miss / Sound;
    for (let Index = 0; Index < Count; Index++)
    {
        const Clock = Index / Rate;
        let Range = 1, Along = 0;
        for (let Step = 0; Step < 3; Step++)                                // Newton: Emit + range(Emit) / c = Clock
        {
            Along = Way * Speed * (Emit - (Heard - Miss / Sound));
            Range = Math.hypot(Along, Miss, Rise);
            const Error = Emit + Range / Sound - Clock, Slope = 1 + Along * Way * Speed / (Range * Sound);
            Emit -= Error / Slope;
        }
        Along = Way * Speed * (Emit - (Heard - Miss / Sound)); Range = Math.hypot(Along, Miss, Rise);
        const Closing = -Along * Way / Range;                              // +1 coming straight in, −1 going away
        const Squeeze = 1 / (1 - Closing * Speed / Sound);                // heard / emitted frequency
        if ((Index & 15) === 0)
        {
            Band.Shape('Band', Clamp(Hz * Squeeze, 300, 19000), 4.5); Edge.Shape('Band', Clamp(Hz * 0.45 * Squeeze, 200, 16000), 1.2);
            Air.Set(Clamp(16000 * Math.pow(3 / Range, 0.3), 6000, 18000));
        }
        const Behind = 0.45 + 0.55 * (1 - Closing) / 2;                    // the wake trails the round
        const Source = Band.Run(Noise.White()) * 1.6 + Edge.Run(Noise.White()) * 0.4 + Rush.Run(Noise.White()) * 0.35;
        const Level = Clamp(Miss / Range, 0, 1) * Math.sqrt(Squeeze) * Behind * Gain * Smoothstep(0, 0.02, Clock) * (1 - Smoothstep(Seconds - 0.03, Seconds, Clock));
        const [GainL, GainR] = PanGains(Clamp(Along / Math.hypot(Along, Miss) * 0.95, -1, 1));
        const Out = Floor.Run(Air.Run(Source * Level));
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    IMPACTS
//------------------------------------------------------------------------------------------------------------------------

/**
 * Plate modes (Follow-up 21): a rectangular plate rings at f(m, n) ∝ (m / a)² + (n / b)², which packs the modes ever
 * closer as the pitch rises (the dense, clangy, inharmonic spectrum of a struck panel, not the few clean partials of a
 * bell). Each mode is weighted by how hard the strike point drives it (sin(mπx)·sin(nπy)) and damped faster the higher
 * it is; a little random detune stands in for an imperfect, bolted plate. Returns Strike modes up to Top Hz.
 */
function PlateModes(Root, Noise, { Aspect = 1.37, Decay = 0.6, Top = 12000, Tilt = 0.5, Lowest = 0, Fall = 0.45 } = {})
{
    const Modes = [], X = Noise.Range(0.15, 0.45), Y = Noise.Range(0.2, 0.48);
    for (let M = 1; M < 14; M++)
        for (let N = 1; N < 14; N++)
        {
            const Hz = Root * (M * M + N * N / (Aspect * Aspect)) / (1 + 1 / (Aspect * Aspect)) * (1 + Noise.Gauss() * 0.012);
            if (Hz > Top || Hz < Lowest) continue;
            const Drive = Math.abs(Math.sin(M * Math.PI * X) * Math.sin(N * Math.PI * Y));
            Modes.push([Hz, Decay * Math.pow(Root / Hz, Fall) * Noise.Range(0.7, 1.2), Drive * Math.pow(Root / Hz, Tilt) * Noise.Range(0.7, 1.1)]);
        }
    return Modes;
}

/**
 * A round into a steel panel (a vehicle door, a target plate). The jacket splatters (a very sharp tick with a short
 * "tss" of fragmenting lead), the panel dents (a dull low "pank"), then the plate clangs with its dense ring and a
 * spray of spall crackles; a glancing hit leaves as a ricochet.
 */
function MetalHit(Left, Right, Rate, Time, Noise, Seed, { Pan = 0, Gain = 1, Glance = false } = {})
{
    const R = (Low, High) => Noise.Range(Low, High), Root = R(120, 190);
    Overpressure(Left, Right, Rate, Time, { Lasts: 0.00022, Gain: 0.35 * Gain, Low: 16000, Pan });
    Strike(Left, Right, Rate, Time, { Modes: [], Burst: 0.0003, Gain: 0, Pan, Seed, Breath: { Low: 3500, High: 12000, Decay: 0.018, Gain: 0.5 * Gain } });
    Strike(Left, Right, Rate, Time, { Modes: [[R(170, 230), 0.05, 1], [R(330, 420), 0.035, 0.6], [R(620, 760), 0.02, 0.35]], Burst: 0.0012, Gain: 0.9 * Gain, Pan, Seed: Seed + 1 });
    Strike(Left, Right, Rate, Time + 0.0004, { Modes: PlateModes(Root, Noise, { Decay: R(1.1, 1.5), Lowest: 300, Fall: 0.3 }), Burst: 0.00025, Gain: 9 * Gain, Pan, Seed: Seed + 2 });
    Scatter(Left, Right, Rate, Time + 0.002, 0.18, 22, (N) => ({ Modes: [[N.Range(5500, 12000), 0.0025, 1]], Burst: 0.00015, Gain: N.Range(0.15, 0.6) * Gain, Pan: Pan + N.Range(-0.35, 0.35) }), { Seed: Seed + 3, Bunch: 2.2 });
    if (Glance) Ricochet(Left, Right, Rate, Time + 0.004, { Hz: R(6500, 8000), Tumble: R(40, 70), Speed: R(450, 560), Seconds: R(0.9, 1.2), Gain: 0.45 * Gain, Pan, Away: Pan > 0 ? -0.9 : 0.9, Toward: 3, Seed: Seed + 4 });
}

/**
 * A round through a window pane. The bullet punches a cone out with a sharp crack, and cracks run out across the pane
 * in a burst of tiny clicks (a few milliseconds); the cracked pane gives one short damped "chink" (its ring dies because
 * the pieces now rub). Pieces break away in the air with a brittle crunch, then the shards land about 0.4 s later
 * (a metre's fall): a dense bright crash first, thinning into scattered tinkles, small shards higher and shorter than
 * big ones, some snapping again as they hit, the last pieces sliding and settling.
 */
function GlassHit(Left, Right, Rate, Time, Noise, Seed, { Pan = 0, Gain = 1 } = {})
{
    const R = (Low, High) => Noise.Range(Low, High);
    const Shard = (N, Size) =>                                   // Size 0 (dust) … 1 (hand-sized): pitch falls with size
    {
        const Hz = Mix(9500, 1500, Size) * N.Range(0.8, 1.25), Decay = Mix(0.03, 0.22, Size) * N.Range(0.7, 1.3);
        return [[Hz, Decay, 1], [Hz * N.Range(1.85, 2.4), Decay * 0.7, N.Range(0.4, 0.8)], [Hz * N.Range(3.3, 4.3), Decay * 0.45, N.Range(0.2, 0.5)]].filter(([F]) => F < 18000);
    };
    Overpressure(Left, Right, Rate, Time, { Lasts: 0.00025, Gain: 0.2 * Gain, Low: 16000, Pan });
    Scatter(Left, Right, Rate, Time, 0.008, 45, (N) => ({ Modes: [[N.Range(3000, 14000), 0.0015, 1]], Burst: 0.0001, Gain: N.Range(0.15, 0.5) * Gain, Pan: Pan + N.Range(-0.15, 0.15) }), { Seed: Seed + 1, Bunch: 1.3 });
    Strike(Left, Right, Rate, Time + 0.0005, { Modes: PlateModes(R(380, 520), Noise, { Aspect: 1.5, Decay: 0.09, Top: 9000, Tilt: 0.4 }), Burst: 0.0003, Gain: 0.45 * Gain, Pan, Seed: Seed + 2,
        Breath: { Low: 2500, High: 11000, Decay: 0.06, Gain: 0.35 * Gain } });
    Scatter(Left, Right, Rate, Time + 0.006, 0.12, 30, (N) => ({ Modes: Shard(N, N.Range(0, 0.6)), Burst: 0.0002, Gain: N.Range(0.4, 1.4) * Gain, Pan: Pan + N.Range(-0.3, 0.3) }), { Seed: Seed + 3, Bunch: 1.5 });
    const Land = Time + R(0.36, 0.44);
    Strike(Left, Right, Rate, Land, { Modes: [], Burst: 0.0003, Gain: 0, Pan, Seed: Seed + 4, Breath: { Low: 2200, High: 13000, Decay: 0.16, Gain: 0.6 * Gain } });   // the crash wash
    Scatter(Left, Right, Rate, Land - 0.01, 0.7, 120, (N, P) => ({ Modes: Shard(N, Math.pow(N.Next(), 2.2) * (1 - P * 0.5)), Burst: 0.0002, Gain: N.Range(0.3, 2.6) * Gain * Math.pow(1 - P, 1.2), Pan: Pan + N.Range(-0.6, 0.6) }), { Seed: Seed + 5, Bunch: 2.6 });
    Scatter(Left, Right, Rate, Land + 0.02, 0.15, 25, (N) => ({ Modes: [[N.Range(4000, 12000), 0.002, 1]], Burst: 0.0001, Gain: N.Range(0.5, 1.6) * Gain, Pan: Pan + N.Range(-0.5, 0.5) }), { Seed: Seed + 6, Bunch: 1.8 });   // pieces snapping again
    Scatter(Left, Right, Rate, Land + 0.35, 0.9, 10, (N) => ({ Modes: Shard(N, N.Range(0.2, 0.7)), Burst: 0.0003, Gain: N.Range(0.15, 0.6) * Gain, Pan: Pan + N.Range(-0.7, 0.7) }), { Seed: Seed + 7, Bunch: 1.2 });       // settling
}

/** One bullet strike on a material at Time. Materials: Sand, Dirt, Rock, Concrete, Wood, Metal, Glass, Water. */
export function DrawHit(Left, Right, Rate, Time, Material, Seed = 400, { Pan = 0, Gain = 1, Bounce: Glance = false } = {})
{
    const Noise = new SeededNoise(Seed), R = (Low, High) => Noise.Range(Low, High);
    switch (Material)
    {
        case 'Sand':
            // Soft thud, a puff, then the spray of grains pattering down with a dry hiss.
            Strike(Left, Right, Rate, Time, { Modes: [[R(160, 210), 0.03, 1], [R(380, 460), 0.02, 0.6]], Burst: 0.003, Gain: 3 * Gain, Pan, Seed });
            Roar(Left, Right, Rate, Time, 0.14, { From: 2600, To: 500, Floor: 120, Gain: 0.5 * Gain, Attack: 0.002, Curve: 2, Seed: Seed + 1 });
            Roar(Left, Right, Rate, Time + 0.02, 0.6, { From: 9000, To: 4500, Floor: 2500, Gain: 0.08 * Gain, Attack: 0.03, Curve: 1.6, Grit: 0.95, Seed: Seed + 2 });
            Scatter(Left, Right, Rate, Time + 0.03, 0.6, 140, (N, P) => ({ Modes: [[N.Range(5000, 11000), 0.002, 1]], Burst: 0.0002, Gain: N.Range(0.1, 0.6) * Gain * (1 - P * 0.6), Pan: Pan + N.Range(-0.4, 0.4) }), { Seed: Seed + 3, Bunch: 1.4 });
            break;
        case 'Dirt':
            Strike(Left, Right, Rate, Time, { Modes: [[R(120, 160), 0.04, 1], [R(300, 380), 0.025, 0.7], [R(800, 1000), 0.012, 0.3]], Burst: 0.0025, Gain: 3.4 * Gain, Pan, Seed });
            Roar(Left, Right, Rate, Time, 0.18, { From: 2200, To: 350, Floor: 90, Gain: 0.55 * Gain, Attack: 0.002, Curve: 2, Grit: 0.5, Seed: Seed + 1 });
            Scatter(Left, Right, Rate, Time + 0.12, 0.45, 16, (N) => ({ Modes: [[N.Range(220, 600), 0.02, 1], [N.Range(900, 1800), 0.01, 0.5]], Burst: 0.0015, Gain: N.Range(0.4, 1.4) * Gain, Pan: Pan + N.Range(-0.5, 0.5) }), { Seed: Seed + 2, Bunch: 1.2 });
            Roar(Left, Right, Rate, Time + 0.05, 0.5, { From: 7000, To: 3500, Floor: 2000, Gain: 0.04 * Gain, Attack: 0.05, Curve: 1.5, Grit: 0.9, Seed: Seed + 3 });
            break;
        case 'Rock':
            // Hard crack (stone rings briefly, high), a sharp snap, chips flying, a little dust; sometimes a ricochet.
            Strike(Left, Right, Rate, Time, { Modes: [[R(2200, 3200), 0.012, 1], [R(4500, 6500), 0.008, 0.7], [R(900, 1400), 0.015, 0.6], [R(8000, 10000), 0.004, 0.4]], Burst: 0.0004, Gain: 4 * Gain, Pan, Seed });
            Overpressure(Left, Right, Rate, Time, { Lasts: 0.00035, Gain: 0.25 * Gain, Low: 14000, Pan });
            Scatter(Left, Right, Rate, Time + 0.01, 0.5, 26, (N) => ({ Modes: [[N.Range(1800, 4500), N.Range(0.004, 0.012), 1], [N.Range(5000, 9000), 0.004, 0.5]], Burst: 0.0002, Gain: N.Range(0.3, 1.2) * Gain, Pan: Pan + N.Range(-0.6, 0.6) }), { Seed: Seed + 1, Bunch: 1.8 });
            Roar(Left, Right, Rate, Time + 0.01, 0.35, { From: 8000, To: 3000, Floor: 1500, Gain: 0.06 * Gain, Attack: 0.01, Curve: 2, Grit: 0.8, Seed: Seed + 2 });
            if (Glance) Ricochet(Left, Right, Rate, Time + 0.006, { Hz: R(6000, 7200), Tumble: R(30, 55), Speed: R(380, 480), Seconds: R(1.0, 1.3), Gain: 0.6 * Gain, Pan, Away: Pan > 0 ? -0.8 : 0.8, Seed: Seed + 3 });
            break;
        case 'Concrete':
            Strike(Left, Right, Rate, Time, { Modes: [[R(1500, 1900), 0.014, 1], [R(3200, 3800), 0.009, 0.6], [R(650, 800), 0.02, 0.6]], Burst: 0.0006, Gain: 3.6 * Gain, Pan, Seed });
            Overpressure(Left, Right, Rate, Time, { Lasts: 0.0005, Gain: 0.3 * Gain, Low: 11000, Pan });
            Roar(Left, Right, Rate, Time + 0.005, 0.7, { From: 7000, To: 1800, Floor: 600, Gain: 0.16 * Gain, Attack: 0.008, Curve: 2.2, Grit: 0.85, Seed: Seed + 1 });   // dust and grit
            Scatter(Left, Right, Rate, Time + 0.02, 0.6, 30, (N) => ({ Modes: [[N.Range(1200, 3200), N.Range(0.005, 0.015), 1]], Burst: 0.0004, Gain: N.Range(0.3, 1) * Gain, Pan: Pan + N.Range(-0.6, 0.6) }), { Seed: Seed + 2, Bunch: 1.6 });
            if (Glance) Ricochet(Left, Right, Rate, Time + 0.006, { Hz: R(5200, 6400), Tumble: R(25, 45), Speed: R(350, 430), Seconds: R(0.8, 1.0), Gain: 0.5 * Gain, Pan, Away: 0.5, Seed: Seed + 3 });
            break;
        case 'Wood':
            // Hollow thunk (plank modes), a low knock through the post, splinters crackling.
            Strike(Left, Right, Rate, Time, { Modes: [[R(320, 420), 0.06, 1], [R(780, 950), 0.04, 0.8], [R(1500, 1900), 0.025, 0.5], [R(2800, 3200), 0.015, 0.3], [R(130, 160), 0.08, 0.7]], Burst: 0.0009, Gain: 4.2 * Gain, Pan, Seed });
            Scatter(Left, Right, Rate, Time + 0.002, 0.14, 22, (N) => ({ Modes: [[N.Range(2800, 7000), 0.003, 1]], Burst: 0.0003, Gain: N.Range(0.4, 1.4) * Gain, Pan: Pan + N.Range(-0.3, 0.3) }), { Seed: Seed + 1, Bunch: 2.2 });
            Scatter(Left, Right, Rate, Time + 0.15, 0.4, 6, (N) => ({ Modes: [[N.Range(600, 1400), 0.02, 1], [N.Range(1800, 3000), 0.01, 0.5]], Burst: 0.0006, Gain: N.Range(0.4, 1) * Gain, Pan: Pan + N.Range(-0.5, 0.5) }), { Seed: Seed + 2 });
            break;
        case 'Metal':
            MetalHit(Left, Right, Rate, Time, Noise, Seed, { Pan, Gain, Glance });
            break;
        case 'Glass':
            GlassHit(Left, Right, Rate, Time, Noise, Seed, { Pan, Gain });
            break;
        case 'Water':
            // Splash: a slap and spray, the cavity's big bubble rising in pitch, then droplets plinking back.
            Roar(Left, Right, Rate, Time, 0.3, { From: 6000, To: 1400, Floor: 300, Gain: 0.45 * Gain, Attack: 0.003, Curve: 2, Grit: 0.5, Seed });
            Strike(Left, Right, Rate, Time, { Modes: [[R(170, 210), 0.03, 1]], Burst: 0.002, Gain: 2.2 * Gain, Pan, Seed: Seed + 1 });
            Bubble(Left, Right, Rate, Time + 0.025, { Hz: R(520, 700), Rise: 1.1, Decay: 0.09, Gain: 0.35 * Gain, Pan });
            {
                let At = Time + 0.12;
                for (let Drop = 0; Drop < 28; Drop++)
                {
                    At += Noise.Range(0.005, 0.045) * (1 + Drop / 14);
                    Bubble(Left, Right, Rate, At, { Hz: R(1300, 3800), Rise: R(0.5, 1.4), Decay: R(0.015, 0.04), Gain: R(0.03, 0.14) * Gain, Pan: Pan + R(-0.6, 0.6) });
                }
            }
            break;
        default: throw new Error(`Unknown material ${Material}`);
    }
}

export const Materials = ['Sand', 'Dirt', 'Rock', 'Concrete', 'Wood', 'Metal', 'Glass', 'Water'];

//------------------------------------------------------------------------------------------------------------------------
//                                                    CUES
//------------------------------------------------------------------------------------------------------------------------

/** The sniper's bolt: lift, draw back (scrape), casing flies out, push forward, lock down, casing lands. */
function BoltCycle(Left, Right, Rate, Time)
{
    Strike(Left, Right, Rate, Time, { Modes: [[1450, 0.03, 1], [3100, 0.02, 0.6]], Burst: 0.0006, Gain: 2.5, Pan: 0.3, Seed: 501 });
    Roar(Left, Right, Rate, Time + 0.05, 0.16, { From: 5000, To: 3000, Floor: 1500, Gain: 0.12, Attack: 0.02, Curve: 0.6, Grit: 0.7, Seed: 502 });
    Strike(Left, Right, Rate, Time + 0.21, { Modes: [[980, 0.04, 1], [2200, 0.03, 0.7], [4300, 0.015, 0.4]], Burst: 0.0008, Gain: 3, Pan: 0.3, Seed: 503 });
    Roar(Left, Right, Rate, Time + 0.42, 0.13, { From: 5200, To: 3200, Floor: 1500, Gain: 0.11, Attack: 0.02, Curve: 0.6, Grit: 0.7, Seed: 504 });
    Strike(Left, Right, Rate, Time + 0.56, { Modes: [[1250, 0.035, 1], [2700, 0.025, 0.6]], Burst: 0.0008, Gain: 3, Pan: 0.3, Seed: 505 });
    Strike(Left, Right, Rate, Time + 0.66, { Modes: [[1650, 0.03, 1], [3500, 0.02, 0.5]], Burst: 0.0006, Gain: 2.4, Pan: 0.3, Seed: 506 });
    DropCasing(Left, Right, Rate, Time + 0.75, 'Rifle762', 'Concrete', { Seed: 507, Gain: 0.14, Pan: 0.7, Height: 1.0 });
}

export const Cues = {
    // ── Weapons ─────────────────────────────────────────────────────────────────────────────────────────────────────
    EnergyLmg: {
        Group: 'Weapons', Label: 'Energy LMG · full mag', Seconds: 5.6, Peak: 0.9,
        Draw(Left, Right, Rate) { Burst(Left, Right, Rate, 0.15, 'EnergyLmg', { Seconds: 10, Seed: 300 }); Finale(Left, Right, Rate, 'Field'); },
    },
    EnergyLmgTurbo: {
        Group: 'Weapons', Label: 'Energy LMG + Turbocharger', Seconds: 4.8, Peak: 0.9,
        Draw(Left, Right, Rate) { Burst(Left, Right, Rate, 0.15, 'EnergyLmg', { Seconds: 10, Turbo: true, Seed: 700 }); Finale(Left, Right, Rate, 'Field'); },
    },
    Lmg: {
        Group: 'Weapons', Label: 'LMG bursts', Seconds: 5.5, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            Burst(Left, Right, Rate, 0.1, 'Lmg', { Seconds: 1.9, Seed: 900 });
            Burst(Left, Right, Rate, 2.6, 'Lmg', { Seconds: 0.5, Seed: 1300 });
            Burst(Left, Right, Rate, 3.5, 'Lmg', { Seconds: 0.7, Seed: 1500 });
            Finale(Left, Right, Rate, Weapons.Lmg.Space);
        },
    },
    Smg: {
        Group: 'Weapons', Label: 'SMG bursts', Seconds: 4.5, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            Burst(Left, Right, Rate, 0.1, 'Smg', { Seconds: 0.25, Seed: 1700 });
            Burst(Left, Right, Rate, 0.75, 'Smg', { Seconds: 0.25, Seed: 1800 });
            Burst(Left, Right, Rate, 1.5, 'Smg', { Seconds: 2.0, Seed: 1900 });     // the rest of the 30-round magazine
            Finale(Left, Right, Rate, Weapons.Smg.Space);
        },
    },
    Sniper: {
        Group: 'Weapons', Label: 'Sniper rifle + bolt', Seconds: 5.5, Peak: 0.94,
        Draw(Left, Right, Rate)
        {
            // .338-class rifle: a 1 ms blast (energy near 400 Hz) clipped hard, the bullet's crack, a long low punch,
            // then the report rolling back off the valley walls for seconds; the bolt is worked afterwards, close and dry.
            Pulse(Left, Right, Rate, 0.1, { Width: 0.001, Drive: 3, Ground: [0.004, 0.6], Gas: { Seconds: 0.06, Hz: 1000, Q: 0.6, Gain: 0.65 }, Low: { Seconds: 0.16, Hz: 62, Gain: 1.1 }, Seed: 2001 });
            Snap(Left, Right, Rate, 0.1002, { Lasts: 0.00036, Gain: 0.55 });
            Strike(Left, Right, Rate, 0.1, { Modes: [[520, 0.09, 1], [1260, 0.06, 0.5], [2380, 0.04, 0.3]], Burst: 0.0005, Gain: 0.6, Seed: 2002 });
            Finale(Left, Right, Rate, 'Valley', { Drive: 2.4 });
            BoltCycle(Left, Right, Rate, 2.6);
        },
    },
    Turret: {
        Group: 'Weapons', Label: 'Turret: wake, track, fire', Seconds: 6.0, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            // Wake: servo traverse whir, three lock-on pips, then a 2.4 s burst with the feed motor and a spin-down.
            Whine(Left, Right, Rate, 0.1, 0.9, { From: 160, To: 420, Gain: 0.12, Curve: 0.8, Harmonics: [1, 0.5, 0.35, 0.2, 0.12], Shape: (P) => Smoothstep(0, 0.15, P) * (1 - Smoothstep(0.8, 1, P)), Wobble: 0.01 });
            Strike(Left, Right, Rate, 1.0, { Modes: [[620, 0.03, 1], [1450, 0.02, 0.6]], Burst: 0.001, Gain: 2.5, Seed: 2101 });
            for (let Pip = 0; Pip < 3; Pip++) Whine(Left, Right, Rate, 1.15 + Pip * 0.16, 0.07, { From: 2400, To: 2400, Gain: 0.08, Curve: 1, Harmonics: [1, 0.2], Wobble: 0 });
            Whine(Left, Right, Rate, 1.63, 0.2, { From: 3200, To: 3200, Gain: 0.08, Curve: 1, Harmonics: [1, 0.2], Wobble: 0 });
            Burst(Left, Right, Rate, 1.95, 'Turret', { Seconds: 2.4, Seed: 2200 });
            Finale(Left, Right, Rate, Weapons.Turret.Space);
        },
    },
    Cannon: {
        Group: 'Weapons', Label: 'Tank cannon', Seconds: 7.0, Peak: 0.94,
        Draw(Left, Right, Rate)
        {
            // 120 mm: a 6 ms blast wave (energy below 70 Hz), the gun's own fireball roaring for a second, the hull
            // ringing; the valley throws it back for seconds. Then, close and dry, the breech opens with a heavy clank
            // and the spent case stub clangs onto the turret floor.
            Pulse(Left, Right, Rate, 0.1, { Width: 0.006, Drive: 2.6, Ground: [0.004, 0.7], Gas: { Seconds: 0.18, Hz: 380, Q: 0.6, Gain: 0.7 }, Low: { Seconds: 0.45, Hz: 42, Gain: 1.3 }, Seed: 2301 });
            Rumble(Left, Right, Rate, 0.1, 2.6, { Hz: 260, EndHz: 45, Gain: 0.4, Attack: 0.01, Seed: 2305 });
            Rumble(Left, Right, Rate, 0.11, 1.2, { Hz: 1800, EndHz: 300, Gain: 0.08, Attack: 0.02, Seed: 2306, Swirl: 0.9 });
            Strike(Left, Right, Rate, 0.12, { Modes: [[85, 0.4, 1], [190, 0.3, 0.7], [410, 0.2, 0.4]], Burst: 0.01, Gain: 1.5, Seed: 2302 });   // the hull rings
            Finale(Left, Right, Rate, 'Valley', { Drive: 2.2 });
            Strike(Left, Right, Rate, 4.2, { Modes: [[260, 0.15, 1], [640, 0.09, 0.7], [1500, 0.05, 0.4]], Burst: 0.002, Gain: 0.35, Pan: 0.2, Seed: 2303 });
            DropCasing(Left, Right, Rate, 4.55, 'TankStub', 'Metal', { Seed: 2304, Gain: 0.04, Pan: 0.4, Height: 0.8 });
            Settle(Left, Right, Rate);
        },
    },
    AutoCannon: {
        Group: 'Weapons', Label: 'Autocannon', Seconds: 5.0, Peak: 0.92,
        Draw(Left, Right, Rate)
        {
            Burst(Left, Right, Rate, 0.1, 'AutoCannon', { Seconds: 1.4, Seed: 2400 });
            Burst(Left, Right, Rate, 2.2, 'AutoCannon', { Seconds: 0.6, Seed: 2500 });
            Finale(Left, Right, Rate, Weapons.AutoCannon.Space);
        },
    },

    // ── Follow-up 19 guns ───────────────────────────────────────────────────────────────────────────────────────────
    Fifty: {
        Group: 'Weapons', Label: '.50 cal HMG bursts', Seconds: 6.0, Peak: 0.92,
        Draw(Left, Right, Rate)
        {
            // Bursts of 8–10 rounds with a pause, the way crews are trained to fire it.
            Burst(Left, Right, Rate, 0.1, 'Fifty', { Seconds: 0.95, Seed: 4000 });
            Burst(Left, Right, Rate, 1.9, 'Fifty', { Seconds: 0.85, Seed: 4100 });
            Burst(Left, Right, Rate, 3.5, 'Fifty', { Seconds: 1.0, Seed: 4200 });
            Finale(Left, Right, Rate, Weapons.Fifty.Space);
        },
    },
    Ak47: {
        Group: 'Weapons', Label: 'AK-47: single shots, then full auto', Seconds: 6.0, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            for (const [Index, Time] of [0.1, 0.65, 1.1].entries()) Burst(Left, Right, Rate, Time, 'Ak47', { Seconds: 0.01, Seed: 4300 + Index * 7 });
            Burst(Left, Right, Rate, 1.9, 'Ak47', { Seconds: 0.5, Seed: 4400 });
            Burst(Left, Right, Rate, 2.9, 'Ak47', { Seconds: 3, Seed: 4500 });       // the rest of the 30-round magazine
            Finale(Left, Right, Rate, Weapons.Ak47.Space);
        },
    },
    M4: {
        Group: 'Weapons', Label: 'M4 carbine: bursts', Seconds: 5.0, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            Burst(Left, Right, Rate, 0.1, 'M4', { Seconds: 0.2, Seed: 4600 });
            Burst(Left, Right, Rate, 0.8, 'M4', { Seconds: 0.2, Seed: 4700 });
            Burst(Left, Right, Rate, 1.5, 'M4', { Seconds: 0.45, Seed: 4800 });
            Burst(Left, Right, Rate, 2.5, 'M4', { Seconds: 1.0, Seed: 4900 });
            Finale(Left, Right, Rate, Weapons.M4.Space);
        },
    },
    Minigun: {
        Group: 'Weapons', Label: 'Minigun: spin-up, 2 s, spin-down', Seconds: 5.5, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            Burst(Left, Right, Rate, 0.2, 'Minigun', { Seconds: 2.0, Seed: 5000 });
            Finale(Left, Right, Rate, Weapons.Minigun.Space);
        },
    },
    Mg42: {
        Group: 'Weapons', Label: 'MG 42 bursts', Seconds: 5.0, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            Burst(Left, Right, Rate, 0.1, 'Mg42', { Seconds: 0.3, Seed: 5100 });
            Burst(Left, Right, Rate, 0.9, 'Mg42', { Seconds: 0.35, Seed: 5200 });
            Burst(Left, Right, Rate, 1.8, 'Mg42', { Seconds: 1.2, Seed: 5300 });
            Finale(Left, Right, Rate, Weapons.Mg42.Space);
        },
    },
    Shotgun: {
        Group: 'Weapons', Label: 'Pump shotgun: three shots', Seconds: 5.0, Peak: 0.92,
        Draw(Left, Right, Rate)
        {
            for (const [Index, Time] of [0.1, 1.4, 2.6].entries()) Burst(Left, Right, Rate, Time, 'Shotgun', { Seconds: 0.01, Seed: 5400 + Index * 13 });
            Finale(Left, Right, Rate, Weapons.Shotgun.Space);
        },
    },
    Pistol: {
        Group: 'Weapons', Label: '9 mm pistol: shots, double taps', Seconds: 4.5, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            for (const [Index, Time] of [0.1, 0.8, 1.05, 1.8, 2.05, 2.3, 3.0].entries()) Burst(Left, Right, Rate, Time, 'Pistol', { Seconds: 0.01, Seed: 5500 + Index * 7 });
            Finale(Left, Right, Rate, Weapons.Pistol.Space);
        },
    },
    AntiMateriel: {
        Group: 'Weapons', Label: '.50 anti-materiel rifle', Seconds: 6.0, Peak: 0.94,
        Draw(Left, Right, Rate)
        {
            for (const [Index, Time] of [0.1, 2.0, 3.4].entries()) Burst(Left, Right, Rate, Time, 'AntiMateriel', { Seconds: 0.01, Seed: 5600 + Index * 13 });
            Finale(Left, Right, Rate, Weapons.AntiMateriel.Space);
        },
    },

    // ── Bullets ─────────────────────────────────────────────────────────────────────────────────────────────────────
    Supersonic: {
        Group: 'Bullets', Label: 'Supersonic crack + far report', Seconds: 4.5, Peak: 0.9,
        Draw(Left, Right, Rate, { Distance = 400 } = {})
        {
            // Downrange of a rifle: each round's shock wave cracks past first; the muzzle report, slower than the
            // bullet, arrives later (Distance / 343 − Distance / 800 s), dull and echoing.
            const Lag = Distance / 343 - Distance / 800;
            const Rounds = [[0.15, 0.00028, 1, -0.4], [1.3, 0.00034, 0.8, 0.5], [1.75, 0.00024, 1, 0.1]];
            for (const [Time, Lasts, Gain, Pan] of Rounds)
            {
                Snap(Left, Right, Rate, Time, { Lasts, Gain, Pan });
                Roar(Left, Right, Rate, Time + 0.0005, 0.05, { From: 9000, To: 3500, Floor: 2000, Gain: 0.15 * Gain, Attack: 0.0005, Curve: 2, Grit: 0.8, Seed: Math.round(Time * 100) });
                // The far report: a blast stretched and dulled by 400 m of air (wider pulse, no highs), then the valley.
                Pulse(Left, Right, Rate, Time + Lag, { Width: 0.0018, Gain: 0.3, Drive: 1.6, Ground: [0.006, 0.7], Dull: 1400, Low: { Seconds: 0.12, Hz: 75, Gain: 0.25 }, Pan: Pan * 0.3, Seed: Math.round(Time * 100) + 7 });
            }
            Place(Left, Right, Rate, 'Valley', { Wet: 0.6 });
            Settle(Left, Right, Rate);
        },
    },
    Whizz: {
        Group: 'Bullets', Label: 'Subsonic whizz-by', Seconds: 2.6, Peak: 0.85,
        Draw(Left, Right, Rate)
        {
            // Three subsonic rounds: close past the right ear, farther right to left, very close left to right.
            Whizz(Left, Right, Rate, 0.05, { Hz: 6800, Speed: 310, Miss: 1.2, Seconds: 0.45, Gain: 0.5, Pan: -1, EndPan: 1, Seed: 2601 });
            Whizz(Left, Right, Rate, 0.8, { Hz: 5600, Speed: 290, Miss: 3, Seconds: 0.5, Gain: 0.45, Pan: 1, EndPan: -1, Seed: 2602 });
            Whizz(Left, Right, Rate, 1.6, { Hz: 7400, Speed: 320, Miss: 0.6, Seconds: 0.4, Gain: 0.55, Pan: -1, EndPan: 1, Seed: 2603 });
            Settle(Left, Right, Rate);
        },
    },
    Ricochet: {
        Group: 'Bullets', Label: 'Ricochets', Seconds: 4.4, Peak: 0.85,
        Draw(Left, Right, Rate)
        {
            // Off rock (flies away), off a steel plate (the fragment zings past the listener's head), off concrete.
            DrawHit(Left, Right, Rate, 0.1, 'Rock', 2701, { Pan: -0.3, Bounce: true });
            DrawHit(Left, Right, Rate, 1.5, 'Metal', 2702, { Pan: 0.4, Bounce: true, Gain: 0.7 });
            DrawHit(Left, Right, Rate, 3.0, 'Concrete', 2703, { Pan: 0, Bounce: true });
            Place(Left, Right, Rate, 'Valley', { Wet: 0.8, Seed: 27 });
            Settle(Left, Right, Rate);
        },
    },

    // ── Shells: spent casings falling (Casings.js) ─────────────────────────────────────────────────────────────────
    ...Object.fromEntries([
        ['ShellsRifle', 'Rifle cases on concrete', 'Rifle762', 'Concrete', 6, 0.62, 4.6],
        ['ShellsPistol', 'Pistol cases on concrete', 'Pistol', 'Concrete', 5, 0.55, 3.8],
        ['ShellsWood', 'Rifle cases on a wooden deck', 'Rifle556', 'Wood', 6, 0.5, 3.8],
        ['ShellsSteel', 'Rifle cases on a steel floor', 'Rifle762', 'Metal', 5, 0.7, 4.6],
        ['ShellsDirt', 'Rifle cases on dirt', 'Rifle762', 'Dirt', 6, 0.45, 3.2],
        ['ShellsShotgun', 'Shotgun shells on concrete', 'Shotgun', 'Concrete', 5, 0.6, 3.6],
        ['ShellsFifty', '.50 cal cases on a steel deck', 'Fifty', 'Metal', 4, 0.85, 4.8],
        ['ShellsCannon', '30 mm cannon cases on steel', 'Cannon30', 'Metal', 4, 0.7, 4.8],
        ['ShellsTank', '120 mm case stub on the turret floor', 'TankStub', 'Metal', 1, 0, 3.0],
    ].map(([Name, Label, Shell, Floor, Count, Gap, Seconds], Index) => [Name, {
        Group: 'Shells', Label, Seconds, Peak: 0.85,
        Draw(Left, Right, Rate)
        {
            for (let Case = 0; Case < Count; Case++)
                DropCasing(Left, Right, Rate, 0.1 + Case * Gap, Shell, Floor, { Seed: 3100 + Index * 101 + Case * 17, Pan: Count > 1 ? -0.5 + Case / (Count - 1) : 0.2, Height: Shell === 'TankStub' ? 0.8 : 1.2 });
            Settle(Left, Right, Rate);
        },
    }])),
    ShellsBrassRain: {
        Group: 'Shells', Label: 'LMG brass and links raining down', Seconds: 5.5, Peak: 0.85,
        Draw(Left, Right, Rate)
        {
            // A 3-second burst's worth (640 rounds/min): cases and belt links pour out and pile up, the last ones roll.
            CasingRain(Left, Right, Rate, Array.from({ length: 32 }, (_, Round) => 0.05 + Round / 10.7), 'Rifle762', 'Concrete', { Seed: 3300, Pan: 0.3, Links: true, Fall: 0.35 });
            Settle(Left, Right, Rate);
        },
    },
    ...Object.fromEntries(Materials.map((Material, Index) => [`Hit${Material}`, {
        Group: 'Impacts', Label: `Bullet into ${Material.toLowerCase()}`, Seconds: Material === 'Metal' ? 1.8 : Material === 'Glass' ? 2.2 : 1.2, Peak: 0.85,
        Draw(Left, Right, Rate) { DrawHit(Left, Right, Rate, 0.03, Material, 3000 + Index * 37); Settle(Left, Right, Rate); },
    }])),
};

/** Renders one cue into fresh stereo buffers, normalized so its loudest sample sits at the cue's Peak. */
export function RenderCue(Name, Rate, Options = {})
{
    const Cue = Cues[Name];
    if (!Cue) throw new Error(`Unknown cue ${Name}`);
    const Seconds = Options.Seconds ?? Cue.Seconds;
    const Left = new Float32Array(Math.ceil(Seconds * Rate)), Right = new Float32Array(Left.length);
    Cue.Draw(Left, Right, Rate, { ...Options, Seconds });
    return Finish(Left, Right, Rate, Options.Peak ?? Cue.Peak ?? 0.9, Seconds);
}

function Finish(Left, Right, Rate, Peak, Seconds)
{
    let Loudest = 0;
    for (let Index = 0; Index < Left.length; Index++) Loudest = Math.max(Loudest, Math.abs(Left[Index]), Math.abs(Right[Index]));
    const Scale = Loudest > 0 ? Peak / Loudest : 1, Fade = Math.round(0.06 * Rate);
    for (let Index = 0; Index < Left.length; Index++)
    {
        const Tail = Math.min(1, (Left.length - 1 - Index) / Fade);
        Left[Index] *= Scale * Tail; Right[Index] *= Scale * Tail;
    }
    return { Left, Right, Seconds, Scale };
}

/**
 * One shot in its weapon's space, for the live bench's hold-to-fire scheduler (each shot carries its own tail, so a
 * held trigger builds the same rolling wash a burst does). Limited like the cues, then set to the weapon's bench Level
 * so every weapon sits at a similar loudness; returns { Left, Right }.
 */
export function RenderShot(Name, Rate, Seed = 1)
{
    const W = Weapons[Name], Seconds = W.Pump ? 2.0 : 1.6;
    const Left = new Float32Array(Math.ceil(Seconds * Rate)), Right = new Float32Array(Left.length);
    DrawShot(Left, Right, Rate, 0.005, Name, 5000 + Seed * 13);
    Place(Left, Right, Rate, W.Space ?? 'Field', { Wet: 0.8, Seed });
    Settle(Left, Right, Rate);
    Limit(Left, Right, Rate, { Drive: 2.2 });
    const Level = 0.3 * (W.Level ?? 1), Fade = Math.round(0.25 * Rate);
    for (let Index = 0; Index < Left.length; Index++) { const Tail = Math.min(1, (Left.length - 1 - Index) / Fade); Left[Index] *= Level * Tail; Right[Index] *= Level * Tail; }
    return { Left, Right };
}

/**
 * A one-second seamless loop of the weapon's spin drive at its top fire rate, so the bench can loop it and set
 * playbackRate = rate / top rate as the spin builds and decays. The drive is noisy machinery (no whole-cycle trick
 * possible), so 1.25 s is rendered and the last quarter second folded linearly over the first: the seam is inaudible.
 */
export function RenderMotor(Name, Rate, Turbo = false)
{
    const W = Weapons[Name], Top = (Turbo && W.Turbo ? W.Turbo.Rate : W.Rate)[1];
    const Fold = Math.round(0.25 * Rate), Count = Rate + Fold, Raw = new Float32Array(Count);
    const Drive = new SpinDrive(Rate, W.Motor, 95, { Loop: true, Turbo: Turbo && !!W.Turbo });
    for (let Index = 0; Index < Rate; Index++) Drive.Run(Top, 1);                  // settle the filters first
    for (let Index = 0; Index < Count; Index++) Raw[Index] = Drive.Run(Top, 1) * W.Motor.Gain;
    const Left = new Float32Array(Rate), Right = new Float32Array(Rate);
    for (let Index = 0; Index < Rate; Index++)
    {
        const Out = Index < Fold ? Raw[Index] * (Index / Fold) + Raw[Rate + Index] * (1 - Index / Fold) : Raw[Index];
        Left[Index] = Out * 0.32; Right[Index] = Out * 0.3;
    }
    return { Left, Right, Top };
}
