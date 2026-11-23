// WAR FOLEY
// 🧩 Battlefield one-shots (Follow-up 19): an explosion in sand (near and far: blast, whump, the sand column tearing the
//    air, then sand raining back down with clods and grains), a mortar launch, an incoming mortar round (whistle with
//    real Doppler geometry, then the impact), a frag grenade (pin, lever, bounce, fuse, blast, fragments), an RPG
//    (back-blast, rocket motor receding, far impact), an artillery barrage rolling in from kilometres away, a military
//    helicopter fly-over (rotor blade passes and slap, tail rotor, turbines), an air-raid siren winding up and down over
//    a town, a tank passing (track links, road wheel squeal, V12 diesel), shell shock (a close blast, then the world
//    muffled with ringing ears and a heartbeat while hearing returns) and a battlefield bed (distant gunfire and
//    explosions, rounds cracking overhead).
// 📝 Same contract as ImpactFoley: Cues (Group 'War', Label, Seconds, Draw) + RenderCue. Moving sources go through
//    Flight(): the output clock is stepped and the emission time advanced by 1 / (1 + radial speed / c), so every
//    pitch carries true Doppler and every level 1 / range, with air absorption by range. Afar() moves a sound away
//    (later is the caller's job; darker by Distant(), quieter by (30 / metres)^0.75, a listening compromise between
//    1 / r and keeping far sounds audible). Original designs; nothing is sampled from any game, film or recording.

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, OnePole, Biquad, Resonator, PanGains, Strike } from './SignalKit.js';
import { Overpressure, Thump, Roar, Scatter, Bounce, Echoes, Distant, Settle } from './ImpactFoley.js';
import { Burst, Snap, Whizz, DrawHit } from './Ballistics.js';
import { Pulse, Rumble, Detonate, Place, Limit } from './Detonation.js';

const Sound = 343;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PIECES
//------------------------------------------------------------------------------------------------------------------------

/**
 * A moving source heard from the origin. Steps the output clock sample by sample and returns, for each, the emission
 * time (d emit / d heard = 1 / (1 + radial speed / c), which is Doppler), the range, the radial speed and the source's
 * x over range (a pan). Path(EmitTime) → [x, y, z] in metres; output sample 0 hears emission time 0.
 */
function Flight(Count, Rate, Path)
{
    const Emit = new Float64Array(Count), Range = new Float32Array(Count), Radial = new Float32Array(Count), Across = new Float32Array(Count);
    const Reach = (Time) => Math.hypot(...Path(Time));
    let Time = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const Here = Reach(Time), Speed = (Reach(Time + 0.001) - Here) / 0.001;
        Emit[Index] = Time; Range[Index] = Here; Radial[Index] = Speed; Across[Index] = Path(Time)[0] / Math.max(1, Here);
        Time += 1 / Rate / (1 + Speed / Sound);
    }
    return { Emit, Range, Radial, Across };
}

/** Air absorption cut-off for a range (as Distant(), but per sample for moving sources). */
const AirCut = (Metres) => Clamp(16000 * Math.pow(30 / Math.max(30, Metres), 0.8), 700, 16000);

/**
 * Draws a sound into scratch buffers (Draw(Left, Right, Rate) at its own time 0), pushes it Metres away (darker and
 * quieter) and adds it at Time with a pan. Metres ≤ 30 just layers it (so a cue can keep echoes off its other parts).
 */
function Afar(Left, Right, Rate, Time, Metres, Seconds, Draw, { Pan = 0 } = {})
{
    const Start = Math.round(Time * Rate), Count = Math.min(Math.ceil(Seconds * Rate), Left.length - Start);
    if (Count <= 0) return;
    const Near = [new Float32Array(Count), new Float32Array(Count)];
    Draw(Near[0], Near[1], Rate);
    Distant(Near[0], Near[1], Rate, Metres);
    const Gain = Math.pow(30 / Math.max(30, Metres), 0.75), [GainL, GainR] = PanGains(Pan);
    for (let Index = 0; Index < Count; Index++)
    {
        Left[Start + Index] += Near[0][Index] * Gain * GainL * Math.SQRT2;
        Right[Start + Index] += Near[1][Index] * Gain * GainR * Math.SQRT2;
    }
}

/**
 * An explosion (Follow-up 20): Detonation.js scales the blast by the charge (positive phase ∝ cube root of the charge,
 * so a 180 g grenade cracks near 300 Hz while a 10 kg shell booms below 60 Hz) and by the ground it goes off on
 * (Open, Sand, Rock, Air), with a turbulent rumble instead of a pitched sine. Charge: kg TNT-equivalent.
 */
function Blast(Left, Right, Rate, Time, { Charge = 4, Ground = 'Open', Pan = 0, Seed = 900, Fire = 0.3, Gain = 1, Metres = 30 } = {})
{
    return Detonate(Left, Right, Rate, Time, { Charge, Ground, Fire, Pan, Seed, Gain, Metres });
}

/** Earth thrown up by a blast falling back: clods thudding, small stones, a dry hiss of dust. */
function Clods(Left, Right, Rate, Time, Seconds, Count, { Pan = 0, Seed = 960, Gain = 1 } = {})
{
    Scatter(Left, Right, Rate, Time, Seconds, Count, (Noise) => ({ Modes: [[Noise.Range(90, 220), 0.04, 1], [Noise.Range(300, 560), 0.02, 0.5]], Burst: Noise.Range(0.002, 0.005), Gain: Noise.Range(0.5, 2) * Gain, Pan: Pan + Noise.Range(-0.8, 0.8) }), { Seed, Bunch: 1.3 });
    Scatter(Left, Right, Rate, Time + 0.1, Seconds, Count, (Noise) => ({ Modes: [[Noise.Range(700, 2200), 0.012, 1], [Noise.Range(2500, 5000), 0.006, 0.5]], Burst: 0.0006, Gain: Noise.Range(0.2, 0.8) * Gain, Pan: Pan + Noise.Range(-0.9, 0.9) }), { Seed: Seed + 1, Bunch: 1.5 });
    Roar(Left, Right, Rate, Time, Seconds * 1.3, { From: 7000, To: 2500, Floor: 1200, Gain: 0.05 * Gain, Width: 1, Grit: 0.9, Seed: Seed + 2, Shape: (P) => Smoothstep(0, 0.15, P) * Math.pow(1 - P, 1.5) });
}

/**
 * Explosion in sand at Distance metres. A charge in or on sand gives a duller crack and a bigger low whump than in the
 * open; the sand column tears the air on the way up, then rains back for seconds: a hiss of grains, a patter of
 * thousands of grains, clods thudding and stones pinging off a vehicle nearby. Far away the grains are lost and only the
 * whump, the dull roar and the echoes off the dunes carry.
 */
function DrawSand(Left, Right, Rate, { Distance = 30 } = {})
{
    const Arrive = 0.08 + Math.min(1.2, Math.max(0, Distance - 30) / Sound) * 0.15, Near = Clamp(1 - (Distance - 30) / 500, 0.08, 1);
    Blast(Left, Right, Rate, Arrive, { Charge: 5, Ground: 'Sand', Fire: 0.15, Seed: 31, Metres: Distance });
    // The sand column: a tearing rush on the way up.
    Roar(Left, Right, Rate, Arrive + 0.01, 1.4, { From: 5500, To: 900, Floor: 300, Gain: 0.6 * Near, Width: 1, Grit: 0.8, Seed: 33, Shape: (P) => Smoothstep(0, 0.08, P) * Math.pow(1 - P, 1.6) });
    // Falling back: a long hiss and thousands of grains, clods first (heavier, shorter flight), stones on a vehicle.
    Roar(Left, Right, Rate, Arrive + 0.6, 5.2, { From: 10000, To: 4000, Floor: 1800, Gain: 0.55 * Math.pow(Near, 1.5), Width: 1, Grit: 0.95, Seed: 34, Shape: (P) => Smoothstep(0, 0.2, P) * Math.pow(1 - P, 1.3) });
    Scatter(Left, Right, Rate, Arrive + 0.7, 4.8, Math.round(1600 * Near), (Noise, P) => ({ Modes: [[Noise.Range(4500, 11000), 0.002, 1]], Burst: 0.0002, Gain: Noise.Range(0.1, 0.7) * (1 - P * 0.7), Pan: Noise.Range(-1, 1) }), { Seed: 35, Bunch: 1.3 });
    Clods(Left, Right, Rate, Arrive + 0.9, 2.6, Math.round(40 * Near) + 4, { Seed: 36, Gain: Near });
    Scatter(Left, Right, Rate, Arrive + 1.1, 2, Math.round(8 * Near), (Noise) => ({ Modes: [[Noise.Range(1400, 2400), 0.25, 1], [Noise.Range(3500, 5200), 0.12, 0.5]], Burst: 0.0003, Gain: Noise.Range(0.15, 0.4), Pan: 0.6 + Noise.Range(-0.2, 0.2) }), { Seed: 37 });
    // Open desert: few surfaces, so sparse late echoes off dunes (the Desert space).
    Place(Left, Right, Rate, 'Desert', { Wet: 1.2 });
    Distant(Left, Right, Rate, Distance);
    Settle(Left, Right, Rate);
}

/** Distant fighting: bursts from several guns at 120–750 m, far explosions, rounds cracking overhead, wind. */
function DrawBattle(Left, Right, Rate, Seconds, Seed = 7000)
{
    const Noise = new SeededNoise(Seed), Guns = ['Ak47', 'M4', 'Mg42', 'Fifty', 'Ak47', 'M4', 'Lmg'];
    Roar(Left, Right, Rate, 0, Seconds, { From: 900, To: 500, Floor: 60, Gain: 0.05, Width: 1, Grit: 0.2, Seed: Seed + 1, Shape: (P) => Smoothstep(0, 0.05, P) * (1 - Smoothstep(0.95, 1, P)) * (0.7 + 0.3 * Math.sin(Tau * P * 1.7)) });
    for (let Time = Noise.Range(0.1, 0.6); Time < Seconds - 1; Time += Noise.Range(0.5, 1.6))
    {
        const Name = Guns[Math.floor(Noise.Next() * Guns.length)], Length = Noise.Range(0.15, 1.4), Metres = Noise.Range(120, 750), Shot = Math.round(Noise.Range(1, 9999));
        Afar(Left, Right, Rate, Time, Metres, Length + 2.5, (L, R, SampleRate) =>
        {
            Burst(L, R, SampleRate, 0.02, Name, { Seconds: Length, Seed: Shot, Brass: false });
            Place(L, R, SampleRate, 'Valley', { Wet: 1.3, Seed: Shot });
        }, { Pan: Noise.Range(-0.9, 0.9) });
    }
    for (let Time = Noise.Range(1, 3); Time < Seconds - 1.5; Time += Noise.Range(2.5, 5))
    {
        const Metres = Noise.Range(600, 2500), Size = Noise.Range(1, 2.5), Bang = Math.round(Noise.Range(1, 9999));
        Afar(Left, Right, Rate, Time, Metres, 5, (L, R, SampleRate) => { Blast(L, R, SampleRate, 0.02, { Charge: Size * 2.5, Seed: Bang, Metres }); Place(L, R, SampleRate, 'Valley', { Wet: 1.4, Seed: Bang }); }, { Pan: Noise.Range(-0.9, 0.9) });
    }
    for (let Time = Noise.Range(1.5, 3); Time < Seconds - 0.5; Time += Noise.Range(1.2, 3.5))
    {
        // Rounds from the far guns passing close: the bullet's crack, no report (that arrives later and dull, above).
        const Pan = Noise.Range(-0.8, 0.8);
        Snap(Left, Right, Rate, Time, { Lasts: Noise.Range(0.0002, 0.0003), Gain: Noise.Range(0.15, 0.35), Pan });
        if (Noise.Next() < 0.4) DrawHit(Left, Right, Rate, Time + 0.01, Noise.Next() < 0.6 ? 'Sand' : 'Rock', Math.round(Noise.Range(1, 9999)), { Pan: Pan * 0.6, Gain: 0.25, Bounce: Noise.Next() < 0.5 });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CUES
//------------------------------------------------------------------------------------------------------------------------

export const Cues = {
    SandBlast: {
        Group: 'War', Label: 'Explosion in sand · 30 m', Seconds: 7.5, Peak: 0.94,
        Draw(Left, Right, Rate, { Distance = 30 } = {}) { DrawSand(Left, Right, Rate, { Distance }); },
    },
    SandBlastFar: {
        Group: 'War', Label: 'Explosion in sand · 350 m', Seconds: 7.0, Peak: 0.55,
        Draw(Left, Right, Rate) { DrawSand(Left, Right, Rate, { Distance: 350 }); },
    },
    MortarLaunch: {
        Group: 'War', Label: 'Mortar: three rounds out', Seconds: 7.5, Peak: 0.92,
        Draw(Left, Right, Rate)
        {
            // Each round is dropped down the tube (fins scraping, a clink), hits the firing pin and leaves with the
            // tube's hollow 'thoonk': a 1.2 m tube closed at the base rings near c / 4L ≈ 70 Hz and its odd partials,
            // here voiced around the mouth's 215 Hz and up. Then the round's whoosh climbing away.
            for (const [Index, Time] of [0.3, 2.7, 4.9].entries())
            {
                Roar(Left, Right, Rate, Time, 0.25, { From: 4000, To: 2000, Floor: 800, Gain: 0.06, Attack: 0.02, Curve: 0.8, Grit: 0.6, Seed: 1100 + Index });
                Strike(Left, Right, Rate, Time + 0.02, { Modes: [[2600, 0.05, 1], [5100, 0.03, 0.4]], Burst: 0.0004, Gain: 1.2, Pan: -0.2, Seed: 1110 + Index });
                const Fire = Time + 0.32;
                Pulse(Left, Right, Rate, Fire, { Width: 0.0028, Gain: 1.1, Drive: 2, Ground: [0.004, 0.6], Gas: { Seconds: 0.05, Hz: 700, Q: 0.6, Gain: 0.5 }, Pan: -0.2, Seed: 1125 + Index });
                Strike(Left, Right, Rate, Fire, { Modes: [[72, 0.25, 0.8], [215, 0.14, 1], [645, 0.07, 0.45], [1075, 0.035, 0.2]], Burst: 0.002, Gain: 5, Pan: -0.2, Seed: 1120 + Index });
                Roar(Left, Right, Rate, Fire + 0.01, 0.7, { From: 3000, To: 600, Floor: 200, Gain: 0.25, Width: 1, Seed: 1130 + Index, Shape: (P) => Smoothstep(0, 0.1, P) * Math.pow(1 - P, 2) });
            }
            Place(Left, Right, Rate, 'Field');
            Settle(Left, Right, Rate);
            Limit(Left, Right, Rate, { Drive: 1.8 });
        },
    },
    MortarIncoming: {
        Group: 'War', Label: 'Incoming mortar round', Seconds: 7.0, Peak: 0.94,
        Draw(Left, Right, Rate)
        {
            // The round comes down at 55° and 210 m/s from behind and lands 47 m away. The fins' whistle (760 Hz at
            // the round) reaches the listener Doppler-shifted: high while it closes fast, falling as it passes
            // overhead and starts to move away, the classic descending whistle, growing as the range shrinks.
            const Start = 0.3, Count = Math.round((7 - Start) * Rate), Angle = 55 * Math.PI / 180, Speed = 210, Length = 1300;
            const Impact = [45, 12, 0], Back = [-Math.cos(Angle), 0, Math.sin(Angle)], Land = Length / Speed;
            const Path = (Time) => { const Remaining = Length - Speed * Math.min(Time, Land); return [Impact[0] + Back[0] * Remaining, Impact[1], Impact[2] + Back[2] * Remaining]; };
            const { Emit, Range, Across } = Flight(Count, Rate, Path);
            const Noise = new SeededNoise(1201), Band = new Biquad(Rate), Air = [new OnePole(Rate, 4000), new OnePole(Rate, 4000)], Offset = Math.round(Start * Rate);
            let Phase = 0, Heard = Count;
            for (let Index = 1; Index < Count; Index++)
            {
                if (Emit[Index] >= Land) { Heard = Index; break; }
                const Step = Emit[Index] - Emit[Index - 1], Hz = 760 * (1 + 0.012 * Math.sin(Tau * 7 * Emit[Index]));
                Phase += Hz * Step;
                if ((Index & 31) === 0) { Band.Shape('Band', Hz * Step * Rate, 7); const Cut = AirCut(Range[Index]); Air[0].Set(Cut); Air[1].Set(Cut); }
                const Level = Clamp(55 / Range[Index], 0, 1.3) * Smoothstep(0, 0.4, Index / Rate);
                const Tone = Air[0].Run(Math.sin(Tau * Phase) * 0.55 + Math.sin(Tau * Phase * 2) * 0.12 + Band.Run(Noise.White()) * 0.9) * Level;
                const [GainL, GainR] = PanGains(Across[Index] * 0.8);
                Left[Offset + Index] += Tone * GainL * 0.5; Right[Offset + Index] += Tone * GainR * 0.5;
            }
            const Hit = Start + Heard / Rate, Pan = 0.6;
            Afar(Left, Right, Rate, Hit, 1, 7 - Hit, (L, R, SampleRate) =>
            {
                Blast(L, R, SampleRate, 0, { Charge: 1, Pan, Seed: 1210, Fire: 0.15, Metres: 47 });   // 81 mm mortar: about 1 kg HE
                Clods(L, R, SampleRate, 0.5, 2.2, 36, { Pan, Seed: 1220 });
                Place(L, R, SampleRate, 'Field', { Seed: 3 });
                Distant(L, R, SampleRate, 47);
            });
            Settle(Left, Right, Rate);
        },
    },
    Grenade: {
        Group: 'War', Label: 'Frag grenade: pin, throw, blast', Seconds: 9.0, Peak: 0.94,
        Draw(Left, Right, Rate)
        {
            // Pin pulled (the ring slides, the pin pings free), thrown, the lever flies off ringing (that starts the
            // 4.5 s fuse), the grenade thuds onto dirt 12 m away and rolls; then the blast with fragments whizzing past.
            // Handling is close to the mic and the blast 12 m off, so the small sounds are lifted about 10 dB (a film
            // perspective) over a quiet outdoor air bed.
            Roar(Left, Right, Rate, 0, 9, { From: 700, To: 400, Floor: 50, Gain: 0.012, Width: 1, Grit: 0.2, Seed: 1300, Shape: (P) => Smoothstep(0, 0.05, P) * (1 - Smoothstep(0.95, 1, P)) });
            const Hand = 3;
            Roar(Left, Right, Rate, 0.1, 0.09, { From: 7000, To: 4000, Floor: 2500, Gain: 0.05 * Hand, Attack: 0.01, Curve: 0.6, Grit: 0.6, Seed: 1301 });
            Strike(Left, Right, Rate, 0.19, { Modes: [[3300, 0.12, 1], [7600, 0.06, 0.4], [11000, 0.03, 0.2]], Burst: 0.0003, Gain: 0.9 * Hand, Pan: 0.1, Seed: 1302 });
            Roar(Left, Right, Rate, 0.9, 0.25, { From: 2500, To: 800, Floor: 300, Gain: 0.08 * Hand, Width: 0.8, Seed: 1303, Shape: (P) => Math.sin(Math.PI * P) });
            Strike(Left, Right, Rate, 1.0, { Modes: [[4100, 0.35, 1], [9200, 0.14, 0.4], [2300, 0.2, 0.3]], Burst: 0.0003, Gain: 0.6 * Hand, Pan: 0.3, Seed: 1304 });
            Bounce(Left, Right, Rate, 1.55, { Modes: [[3600, 0.08, 1], [8100, 0.04, 0.4]], Hits: 3, First: 0.12, Gain: 0.25 * Hand, Pan: 0.5, Seed: 1305 });
            Strike(Left, Right, Rate, 1.9, { Modes: [[180, 0.03, 1], [420, 0.02, 0.5], [1100, 0.01, 0.3]], Burst: 0.003, Gain: 1.2 * Hand, Pan: 0.4, Seed: 1306 });
            Strike(Left, Right, Rate, 2.15, { Modes: [[200, 0.025, 1], [470, 0.015, 0.5]], Burst: 0.002, Gain: 0.6 * Hand, Pan: 0.45, Seed: 1307 });
            Roar(Left, Right, Rate, 2.2, 0.55, { From: 3000, To: 1500, Floor: 600, Gain: 0.03 * Hand, Grit: 0.9, Seed: 1308, Shape: (P) => Smoothstep(0, 0.1, P) * Math.pow(1 - P, 1.5) });
            const Bang = 1.0 + 4.5 + 12 / Sound;
            Afar(Left, Right, Rate, Bang, 1, 9 - Bang, (L, R, SampleRate) =>
            {
                Blast(L, R, SampleRate, 0, { Charge: 0.18, Pan: 0.4, Seed: 1310, Fire: 0.05, Metres: 12 });   // M67-class: 180 g filler, a sharp crack
                const Noise = new SeededNoise(1311);
                for (let Piece = 0; Piece < 6; Piece++)
                {
                    const Side = Noise.Next() < 0.5 ? -1 : 1;
                    Whizz(L, R, SampleRate, 0.01 + Noise.Range(0, 0.15), { From: Noise.Range(4000, 5500), To: Noise.Range(1500, 2500), Seconds: Noise.Range(0.12, 0.2), Gain: 0.25, Pan: -Side, EndPan: Side, Seed: 1312 + Piece });
                }
                Clods(L, R, SampleRate, 0.35, 1.6, 24, { Pan: 0.4, Seed: 1320, Gain: 0.7 });
                Place(L, R, SampleRate, 'Forest', { Seed: 4 });
            });
            Settle(Left, Right, Rate);
        },
    },
    RpgRocket: {
        Group: 'War', Label: 'RPG: launch, rocket, far impact', Seconds: 7.0, Peak: 0.94,
        Draw(Left, Right, Rate)
        {
            // The booster kicks it out with a sharp pop and a back-blast behind the shooter; the rocket motor lights
            // about 11 m out and screams away at about 295 m/s, so its roar is pulled down in pitch (Doppler) and
            // darkens and fades with range; it strikes about 300 m away and the blast arrives 0.9 s after that.
            Afar(Left, Right, Rate, 0.2, 1, 3, (L, R, SampleRate) =>
            {
                Pulse(L, R, SampleRate, 0, { Width: 0.0016, Gain: 1.2, Drive: 2.6, Ground: [0.003, 0.6], Low: { Seconds: 0.05, Hz: 90, Gain: 0.4 }, Seed: 1400 });
                Roar(L, R, SampleRate, 0, 0.45, { From: 9000, To: 600, Floor: 120, Gain: 0.9, Attack: 0.002, Curve: 2.2, Grit: 0.6, Seed: 1401 });
                Place(L, R, SampleRate, 'Urban', { Wet: 0.7, Seed: 5 });
            });
            const Ignite = 0.24, Burn = 1.0, Count = Math.round(2.6 * Rate), Offset = Math.round(Ignite * Rate);
            const { Emit, Range, Across } = Flight(Count, Rate, (Time) => [11 + 295 * Math.min(Time, Burn), 4, 1.5]);
            const Noise = new SeededNoise(1402), Band = new Biquad(Rate), Air = new OnePole(Rate, 8000), Crackle = new OnePole(Rate, 60);
            let Heard = Count;
            for (let Index = 1; Index < Count; Index++)
            {
                if (Emit[Index] >= Burn) { Heard = Index; break; }
                const Shift = (Emit[Index] - Emit[Index - 1]) * Rate;
                if ((Index & 31) === 0) { Band.Shape('Band', 1800 * Shift, 0.9); Air.Set(AirCut(Range[Index])); }
                const Rough = Clamp(Crackle.Run(Noise.White() * 8) * 3 + 0.5, 0, 3);
                const Out = Air.Run(Band.Run(Noise.White()) * Rough) * Clamp(14 / Range[Index], 0, 1.2) * Smoothstep(0, 0.01, Index / Rate);
                const [GainL, GainR] = PanGains(Across[Index] * 0.5);
                Left[Offset + Index] += Out * GainL * 0.7; Right[Offset + Index] += Out * GainR * 0.7;
            }
            const Hit = Ignite + Heard / Rate;
            Afar(Left, Right, Rate, Hit, 306, 7 - Hit, (L, R, SampleRate) =>
            {
                Blast(L, R, SampleRate, 0.02, { Charge: 1.5, Ground: 'Rock', Seed: 1410, Fire: 0.6, Metres: 306 });   // warhead plus the vehicle's fuel
                Strike(L, R, SampleRate, 0.02, { Modes: [[46, 0.5, 1.4], [118, 0.32, 0.9], [290, 0.18, 0.5], [720, 0.08, 0.25]], Burst: 0.004, Gain: 1.2, Seed: 1411 });   // a vehicle hull struck
                Place(L, R, SampleRate, 'Urban', { Wet: 1.2, Seed: 6 });
            }, { Pan: 0.35 });
            Settle(Left, Right, Rate);
        },
    },
    Barrage: {
        Group: 'War', Label: 'Artillery barrage, 1–5 km', Seconds: 15.0, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            // Heavy shells landing 1.2–5 km away: each a deep boom with its high end absorbed by the air, rolling
            // echoes off hills, over a constant low rumble; they overlap into continuous thunder.
            Roar(Left, Right, Rate, 0, 15, { From: 300, To: 120, Floor: 18, Gain: 0.12, Width: 1, Seed: 1501, Shape: (P) => Smoothstep(0, 0.1, P) * (1 - Smoothstep(0.9, 1, P)) });
            const Noise = new SeededNoise(1502);
            for (let Round = 0; Round < 15; Round++)
            {
                const Time = Noise.Range(0.2, 12.5), Metres = Noise.Range(1200, 5000), Size = Noise.Range(1.5, 3), Seed = 1510 + Round * 7;
                Afar(Left, Right, Rate, Time, Metres, 15 - Time, (L, R, SampleRate) =>
                {
                    Blast(L, R, SampleRate, 0.02, { Charge: Size * 5, Seed, Metres });                  // 155 mm class: 7–15 kg
                    Place(L, R, SampleRate, 'Valley', { Wet: 1.6, Seed });
                }, { Pan: Noise.Range(-0.9, 0.9) });
            }
            Settle(Left, Right, Rate);
        },
    },
    Helicopter: {
        Group: 'War', Label: 'Military helicopter fly-over', Seconds: 16.0, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            // Black Hawk-class numbers: 4-blade main rotor at 258 rpm (17.2 blade passes a second, the 'wop'), 4-blade
            // tail rotor at 1190 rpm (79 Hz buzz), twin turbine whine and gearbox whine. Fly-over at 55 m height, 40 m
            // to the side, 62 m/s (120 knots). Blade slap is stronger on the approach. Every pitch is Doppler-shifted.
            const Count = Math.round(16 * Rate), { Emit, Range, Radial, Across } = Flight(Count, Rate, (Time) => [-500 + 62 * Time, 40, 55]);
            const Noise = new SeededNoise(1601), Loading = new Biquad(Rate).Shape('Band', 520, 1.2), Slap = new Biquad(Rate).Shape('Band', 1800, 0.9), Engine = new Biquad(Rate).Shape('Band', 2500, 0.8);
            const Air = [new OnePole(Rate, 8000), new OnePole(Rate, 8000)];
            let Blade = 0, Tail = 0, Turbine = 0, Turbine2 = 0, Gear = 0;
            for (let Index = 1; Index < Count; Index++)
            {
                const Step = Emit[Index] - Emit[Index - 1];
                Blade += 17.2 * Step; Tail += 79 * Step; Turbine += 3300 * Step; Turbine2 += 5100 * Step; Gear += 1150 * Step;
                if ((Index & 31) === 0) { const Cut = AirCut(Range[Index]); Air[0].Set(Cut); Air[1].Set(Cut * 0.6); }
                const Spot = (Blade % 1) - 0.5, Pulse = Math.exp(-Spot * Spot / 0.0012), Sharp = Math.exp(-Spot * Spot / 0.00015);
                const Approach = Clamp(-Radial[Index] / 62, 0, 1);
                let Out = -Pulse * 0.5 + Loading.Run(Noise.White()) * Pulse * 1.4 + Slap.Run(Noise.White()) * Sharp * (0.4 + 0.8 * Approach);
                for (let Partial = 1; Partial <= 6; Partial++) Out += Math.sin(Tau * Blade * Partial) * 0.12 / Partial;
                for (let Partial = 1; Partial <= 8; Partial++) Out += Math.sin(Tau * Tail * Partial) * 0.07 / Partial;
                Out += Math.sin(Tau * Turbine) * 0.02 + Math.sin(Tau * Turbine2) * 0.012 + Math.sin(Tau * Gear) * 0.015 + Engine.Run(Noise.White()) * 0.06;
                const Level = Clamp(60 / Range[Index], 0, 1.4), Shaded = Mix(Air[0].Run(Out), Air[1].Run(Out), 0.3) * Level;
                const [GainL, GainR] = PanGains(Across[Index] * 0.7);
                Left[Index] += Shaded * GainL; Right[Index] += Shaded * GainR;
            }
            Echoes(Left, Right, Rate, { Taps: [[0.18, 0.2, -0.6], [0.45, 0.12, 0.6]], Dark: 2500, Wash: 0.15 });
            Settle(Left, Right, Rate);
        },
    },
    AirRaidSiren: {
        Group: 'War', Label: 'Air-raid siren over a town', Seconds: 18.0, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            // A two-rotor mechanical siren (10 and 12 ports, a minor-third chord, 480 / 576 Hz at full speed). The
            // motor is switched on and off and the rotor's inertia makes the wail: up over about 5 s, down slower.
            // The horn head turns (level swells every 7 s); heard 200 m away across a town, echoing off buildings.
            const Count = Math.round(18 * Rate), Noise = new SeededNoise(1701), Whoosh = new Biquad(Rate).Shape('Band', 900, 1.5), Horn = new Biquad(Rate).Shape('Low', 3200, 0.7);
            let Speed = 0, One = 0, Two = 0;
            for (let Index = 0; Index < Count; Index++)
            {
                const Time = Index / Rate, On = (Time > 0.3 && Time < 6.5) || (Time > 9 && Time < 12);
                Speed += ((On ? 1 : 0) - Speed) / ((On ? 2.2 : 3.4) * Rate);
                One += 48 * Speed * 10 / Rate; Two += 48 * Speed * 12 / Rate;
                const Chopped = Math.tanh(2.5 * Math.sin(Tau * One)) + Math.tanh(2.5 * Math.sin(Tau * Two)) * 0.85;
                const Head = 0.6 + 0.4 * Math.cos(Tau * Time / 7);
                const Out = (Horn.Run(Chopped) * Math.pow(Speed, 1.5) + Whoosh.Run(Noise.White()) * Speed * Speed * 0.15) * Head * 0.4;
                const [GainL, GainR] = PanGains(0.3 * Math.sin(Tau * Time / 7));
                Left[Index] += Out * GainL; Right[Index] += Out * GainR;
            }
            Echoes(Left, Right, Rate, { Taps: [[0.12, 0.4, -0.6], [0.31, 0.3, 0.6], [0.55, 0.2, -0.2], [0.9, 0.12, 0.5]], Dark: 2500, Wash: 0.45 });
            Distant(Left, Right, Rate, 200);
            Settle(Left, Right, Rate);
        },
    },
    TankDriveBy: {
        Group: 'War', Label: 'Tank passing', Seconds: 14.0, Peak: 0.9,
        Draw(Left, Right, Rate)
        {
            // 8 m/s (29 km/h), 14 m away. Track links (0.15 m pitch) clank onto the sprockets 53 times a second, with
            // the odd slap of slack track; road wheels squeal on and off; a V12 diesel at 1900 rpm (190 Hz firing,
            // 15.8 Hz half-order roughness) and its exhaust; ground rumble.
            const Count = Math.round(14 * Rate), { Emit, Range, Across } = Flight(Count, Rate, (Time) => [-56 + 8 * Time, 14, 1]);
            const Noise = new SeededNoise(1801), Links = [[620, 0.02], [1350, 0.015], [2750, 0.01], [4100, 0.006]].map(([Hz, Decay]) => new Resonator(Rate, Hz, Decay));
            const Squeal = new OnePole(Rate, 0.4), Pitch = new OnePole(Rate, 0.3), Exhaust = new Biquad(Rate).Shape('Low', 380, 0.8), Ground = new Biquad(Rate).Shape('Low', 60, 0.7);
            const Air = new OnePole(Rate, 8000);
            let Link = 0, Wheel = 0, Engine = 0, Drive = 0;
            for (let Index = 1; Index < Count; Index++)
            {
                const Step = Emit[Index] - Emit[Index - 1];
                Link += 53.3 * Step; Engine += 15.8 * Step;
                const Kick = Link >= 1 ? (Link -= 1, Noise.Range(0.4, 1) * (Noise.Next() < 0.03 ? 3 : 1)) : 0;
                let Clank = 0;
                for (const Ring of Links) Clank += Ring.Run(Kick) / Ring.Norm;
                const Hz = 1400 + 1200 * Clamp(Pitch.Run(Noise.White() * 40) + 0.5, 0, 1);
                Wheel += Hz * Step;
                const Squeak = Math.sin(Tau * Wheel) * Clamp(Squeal.Run(Noise.White() * 60) * 3 - 0.2, 0, 1);
                let Tone = 0;
                for (let Partial = 1; Partial <= 14; Partial++) Tone += Math.sin(Tau * Engine * Partial + Partial * 0.7) * (Partial === 12 ? 0.25 : 0.06) / Math.sqrt(Partial);
                const Fire = Math.pow(0.5 + 0.5 * Math.sin(Tau * Engine * 12), 4);
                Drive += 620 * Step;
                const Out = Clank * 0.035 + Squeak * 0.05 + Tone * 0.5 + Exhaust.Run(Noise.White()) * (0.4 + Fire) * 0.5 + Ground.Run(Noise.White()) * 1.2 + Math.sin(Tau * Drive) * 0.01;
                if ((Index & 31) === 0) Air.Set(AirCut(Range[Index]));
                const Level = Clamp(18 / Range[Index], 0, 1.3), Shaded = Air.Run(Out) * Level;
                const [GainL, GainR] = PanGains(Across[Index] * 0.7);
                Left[Index] += Shaded * GainL; Right[Index] += Shaded * GainR;
            }
            Echoes(Left, Right, Rate, { Taps: [[0.15, 0.2, -0.6], [0.38, 0.12, 0.6]], Dark: 2000, Wash: 0.15 });
            Settle(Left, Right, Rate);
        },
    },
    Deafened: {
        Group: 'War', Label: 'Shell shock: blast, muffled, ringing', Seconds: 14.0, Peak: 0.94,
        Draw(Left, Right, Rate)
        {
            // The war-film effect, after real temporary threshold shift: fighting all round, a shell lands 10 m away,
            // then everything is muffled (a low-pass that starts near 180 Hz) and quieter, the ears ring (a beating
            // 4.15 kHz tone) and a heartbeat thuds; hearing comes back over about ten seconds.
            const Hit = 1.5;
            DrawBattle(Left, Right, Rate, 14, 7300);
            Blast(Left, Right, Rate, Hit, { Charge: 7, Pan: -0.2, Seed: 1901, Fire: 0.3, Metres: 10 });
            Clods(Left, Right, Rate, Hit + 0.5, 2.5, 40, { Pan: -0.2, Seed: 1902 });
            const Shades = [[new OnePole(Rate, 16000), new OnePole(Rate, 16000)], [new OnePole(Rate, 16000), new OnePole(Rate, 16000)]];
            for (let Index = 0; Index < Left.length; Index++)
            {
                const Time = Index / Rate, After = Time - Hit - 0.03;
                const Back = After < 0 ? 1 : 1 - Math.exp(-After / 4.5), Cut = After < 0 ? 16000 : 180 * Math.pow(16000 / 180, Math.pow(Back, 1.5));
                if ((Index & 31) === 0) for (const Side of Shades) for (const Pole of Side) Pole.Set(Cut);
                const Gain = After < 0 ? 1 : Mix(0.3, 1, Back);
                Left[Index] = Shades[0][1].Run(Shades[0][0].Run(Left[Index])) * Gain;
                Right[Index] = Shades[1][1].Run(Shades[1][0].Run(Right[Index])) * Gain;
                if (After > 0)
                {
                    const Ring = (Math.sin(Tau * 4150 * Time) + Math.sin(Tau * 4163 * Time) * 0.7) * 0.05 * Smoothstep(0, 0.3, After) * Math.exp(-After / 5);
                    Left[Index] += Ring; Right[Index] += Ring;
                }
            }
            for (let Beat = Hit + 0.8; Beat < 13.5; Beat += 60 / 72)
            {
                const Fade = Math.exp(-(Beat - Hit) / 6);
                Thump(Left, Right, Rate, Beat, { Hz: 52, EndHz: 36, Decay: 0.14, Gain: 0.5 * Fade, Drive: 1.4 });
                Thump(Left, Right, Rate, Beat + 0.28, { Hz: 58, EndHz: 40, Decay: 0.11, Gain: 0.32 * Fade, Drive: 1.4 });
            }
            Settle(Left, Right, Rate);
        },
    },
    Battlefield: {
        Group: 'War', Label: 'Battlefield, distant fighting', Seconds: 20.0, Peak: 0.85,
        Draw(Left, Right, Rate) { DrawBattle(Left, Right, Rate, 20, 7000); Settle(Left, Right, Rate); },
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
    const Scale = Loudest > 0 ? (Options.Peak ?? Cue.Peak ?? 0.9) / Loudest : 1, Fade = Math.round(0.08 * Rate);
    for (let Index = 0; Index < Left.length; Index++)
    {
        const Tail = Math.min(1, (Left.length - 1 - Index) / Fade);
        Left[Index] *= Scale * Tail; Right[Index] *= Scale * Tail;
    }
    return { Left, Right, Seconds };
}
