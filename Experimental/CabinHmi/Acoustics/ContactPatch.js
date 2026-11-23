// CONTACT PATCH
// 🧩 Everything the tyres make: the rolling sound over seven surfaces (asphalt, wet, gravel, dirt, grass, cobbles,
//    rumble strip, smooth concrete), wind over the body, and tyre squeal (cornering, drifting), lock-up skid and wheelspin howl.
// 📝 Like PulseTrainEngine these are live voices: Feed() the speed and slip each block, Render() adds stereo samples.
//    Surfaces are built from their physics: tread-block impacts and air pumping (asphalt), water film and spray (wet),
//    discrete stone hits and underbody pings (gravel), periodic stone or strip impacts through the suspension (cobbles,
//    rumble strip). Squeal (Follow-up 22) is the rubber singing: three sliding tyres, each a warbling tone with overtones
//    through the belt resonances, set per surface.

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, OnePole, Biquad, Resonator, Slew, Wander, PanGains, SoftClip, NarrowTone } from './SignalKit.js';

/** Surface recipes. Gain trims overall loudness; Roll is the tread/texture band; the rest drive each layer. */
export const Surfaces = {
    Asphalt:     { Label: 'Asphalt', Roll: [700, 1.1], Rumble: 0.55, Hiss: 0.18, Stones: 0, Water: 0, Spacing: 0, Thump: 0, Grip: 1, Gain: 1, Squeal: { Tone: 1, Pitch: 1 } },
    Wet:         { Label: 'Wet asphalt', Roll: [620, 0.9], Rumble: 0.45, Hiss: 0.45, Stones: 0, Water: 1, Spacing: 0, Thump: 0, Grip: 0.7, Gain: 1, Squeal: { Tone: 0.18, Pitch: 0.9 } },
    Gravel:      { Label: 'Gravel', Roll: [900, 0.6], Rumble: 0.9, Hiss: 0.25, Stones: 1, Water: 0, Spacing: 0, Thump: 0.2, Grip: 0.55, Gain: 1.35, Squeal: { Tone: 0, Pitch: 1 } },
    Dirt:        { Label: 'Dirt track', Roll: [320, 0.7], Rumble: 1.1, Hiss: 0.08, Stones: 0.18, Water: 0, Spacing: 0, Thump: 0.55, Grip: 0.65, Gain: 1.1, Squeal: { Tone: 0, Pitch: 1 } },
    Grass:       { Label: 'Grass', Roll: [1800, 0.5], Rumble: 0.7, Hiss: 0.35, Stones: 0, Water: 0.15, Spacing: 0, Thump: 0.35, Grip: 0.45, Gain: 0.5, Squeal: { Tone: 0, Pitch: 1 } },
    Cobbles:     { Label: 'Cobblestones', Roll: [550, 0.9], Rumble: 0.8, Hiss: 0.15, Stones: 0, Water: 0, Spacing: 0.115, Thump: 1, Grip: 0.85, Gain: 0.8, Squeal: { Tone: 0.6, Pitch: 1.04 } },
    RumbleStrip: { Label: 'Rumble strip', Roll: [700, 1.1], Rumble: 0.6, Hiss: 0.18, Stones: 0, Water: 0, Spacing: 0.32, Thump: 1.4, Grip: 1, Gain: 0.9, Squeal: { Tone: 0.9, Pitch: 1 } },
    Concrete:    { Label: 'Smooth concrete', Roll: [820, 1.2], Rumble: 0.4, Hiss: 0.14, Stones: 0, Water: 0, Spacing: 0, Thump: 0, Grip: 1, Gain: 0.85, Squeal: { Tone: 1.35, Pitch: 1.22 } },   // a garage or warehouse floor: the shrillest squeal
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ROLLING
//------------------------------------------------------------------------------------------------------------------------

/** Rolling, road texture and wind for one surface at a time; changing Surface crossfades over ~0.25 s. */
export class SurfaceRoll
{
    constructor(Rate, { Seed = 21, Pickup = 'Chase' } = {})
    {
        this.Rate = Rate;
        this.Pickup = Pickup;
        this.Noise = new SeededNoise(Seed);
        this.Speed = new Slew(Rate, 0.05, 0);
        // Separate levels (bench Mix): Road = tyres on the surface (roar, hiss, tread, stones, water, knocks), Wind = air.
        this.RoadLevel = 1; this.WindLevel = 1;
        this.Weights = Object.fromEntries(Object.keys(Surfaces).map((Name) => [Name, new Slew(Rate, 0.12, Name === 'Asphalt' ? 1 : 0)]));
        this.Surface = 'Asphalt';

        // Shared layers, re-tuned per surface by weight.
        this.RollBands = Object.fromEntries(Object.entries(Surfaces).map(([Name, S]) => [Name, [new Biquad(Rate).Shape('Band', S.Roll[0], S.Roll[1]), new Biquad(Rate).Shape('Band', S.Roll[0], S.Roll[1])]]));
        this.Rumble = [new Biquad(Rate).Shape('Low', 160, 0.8), new Biquad(Rate).Shape('Low', 160, 0.8)];
        this.Hiss = [new Biquad(Rate).Shape('High', 2600, 0.7), new Biquad(Rate).Shape('High', 2600, 0.7)];
        this.Wind = [new Biquad(Rate).Shape('Band', 380, 0.5), new Biquad(Rate).Shape('Band', 420, 0.5)];
        this.Gust = new Wander(Rate, 0.7, Seed + 1);
        this.TreadPhase = 0;

        // Wet: water film sizzle and spray bursts.
        this.Spray = [new Biquad(Rate).Shape('Band', 4200, 0.6), new Biquad(Rate).Shape('Band', 4600, 0.6)];
        this.Splash = 0; this.SplashBand = new Biquad(Rate).Shape('Band', 1400, 0.8);

        // Gravel: a pool of stone resonators re-tuned on each hit; underbody pings ring longer.
        this.StonePool = Array.from({ length: 24 }, () => ({ Ring: new Resonator(Rate, 3000, 0.01), Side: 0, Hit: 0 }));
        this.StoneNext = 0;
        this.Ping = Array.from({ length: 4 }, () => ({ Ring: new Resonator(Rate, 2600, 0.25), Side: 0, Hit: 0 }));
        this.PingNext = 0;
        this.Crunch = [new Biquad(Rate).Shape('Band', 1900, 0.8), new Biquad(Rate).Shape('Band', 2100, 0.8)];
        this.CrunchEnv = 0;

        // Periodic impacts (cobbles, strip): suspension and body modes struck at the spacing rate.
        this.Impact = 0;
        this.Distance = 0; this.LastStone = 0;
        this.Body = [new Resonator(Rate, 68, 0.12), new Resonator(Rate, 140, 0.08), new Resonator(Rate, 310, 0.05)];
        this.Tread = [new Resonator(Rate, 900, 0.015), new Resonator(Rate, 1500, 0.01)];
        this.Bump = new Resonator(Rate, 32, 0.3);                     // tyre sidewall / unsprung mass hop, struck only

        // Cabin colouring: the car body passes lows and cuts highs.
        this.Cabin = [new Biquad(Rate).Shape('HighShelf', 900, 0.7, -14), new Biquad(Rate).Shape('HighShelf', 900, 0.7, -14)];
        this.CabinBoom = [new Biquad(Rate).Shape('Peak', 55, 1, 6), new Biquad(Rate).Shape('Peak', 55, 1, 6)];
    }

    Feed({ Speed = 0, Surface = this.Surface })
    {
        this.Speed.Target = Math.max(0, Speed);
        if (Surfaces[Surface]) this.Surface = Surface;
        for (const [Name, Weight] of Object.entries(this.Weights)) Weight.Target = Name === this.Surface ? 1 : 0;
    }

    Render(Left, Right, Offset = 0, Count = Left.length - Offset)
    {
        const Rate = this.Rate, Noise = this.Noise;
        const Names = Object.keys(Surfaces);
        for (let Index = Offset; Index < Offset + Count; Index++)
        {
            const Speed = this.Speed.Run();
            const Kmh = Speed * 3.6;
            const Moving = Smoothstep(0.3, 3, Speed);
            // Tyre roar grows ~8 dB per doubling of speed up to 150 km/h; above that it is eased to ~3 dB per doubling
            // (a listening choice, not physics: at 300+ km/h the real roar buries the engine, which wears on the ear).
            const Level = (Kmh <= 150 ? Math.pow(Kmh / 100, 1.4) : 1.763 * Math.pow(Kmh / 150, 0.5)) * Moving;
            this.Distance += Speed / Rate;

            // Blend surface recipes into a working set.
            let Rumble = 0, Hiss = 0, Stones = 0, Water = 0, Thump = 0, Spacing = 0, Spaced = 0, Loudness = 0;
            const Rolls = [0, 0];
            const White = [Noise.White(), Noise.White()];
            for (const Name of Names)
            {
                const Weight = this.Weights[Name].Run();
                if (Weight < 1e-4) continue;
                const S = Surfaces[Name];
                Loudness += S.Gain * Weight;
                Rumble += S.Rumble * Weight; Hiss += S.Hiss * Weight; Stones += S.Stones * Weight; Water += S.Water * Weight; Thump += S.Thump * Weight;
                if (S.Spacing) { Spacing += S.Spacing * Weight; Spaced += Weight; }
                Rolls[0] += this.RollBands[Name][0].Run(White[0]) * Weight;
                Rolls[1] += this.RollBands[Name][1].Run(White[1]) * Weight;
            }
            if (Spaced > 0) Spacing /= Spaced;

            // Tread pattern tone: blocks hitting the road at Speed / pitch (≈ 30 mm), randomized pitch sequence.
            this.TreadPhase += Speed / 0.031 / Rate;
            let Tread = 0;
            if (this.TreadPhase >= 1) { this.TreadPhase -= 1 + (Noise.Next() - 0.5) * 0.3; Tread = 1; }
            const TreadRing = (this.Tread[0].Run(Tread) + this.Tread[1].Run(Tread) * 0.6) * 0.5;

            let OutL = (Rolls[0] * 2.2 + TreadRing * 0.5) * Level;
            let OutR = (Rolls[1] * 2.2 + TreadRing * 0.5) * Level;
            OutL += this.Rumble[0].Run(Noise.White()) * Rumble * Level * 1.4;
            OutR += this.Rumble[1].Run(Noise.White()) * Rumble * Level * 1.4;
            OutL += this.Hiss[0].Run(White[0]) * Hiss * Level * 0.5;
            OutR += this.Hiss[1].Run(White[1]) * Hiss * Level * 0.5;

            // Wind: rises with speed squared, gusting.
            const Wind = Math.pow(Kmh / 200, 2.2) * (0.8 + 0.25 * this.Gust.Run());
            const WindL = this.Wind[0].Run(Noise.Pink()) * Wind * 0.9;
            const WindR = this.Wind[1].Run(Noise.Pink()) * Wind * 0.9;

            // Wet: continuous sizzle and spray, irregular splashes from standing water.
            if (Water > 0.01)
            {
                const Sizzle = (Noise.Next() < 0.004 * (1 + Kmh / 40) ? Noise.Range(1, 3) : 0) * Noise.White();
                OutL += (this.Spray[0].Run(Noise.White()) * 0.55 + Sizzle * 0.15) * Water * Level * 1.2;
                OutR += (this.Spray[1].Run(Noise.White()) * 0.55 + Sizzle * 0.15) * Water * Level * 1.2;
                if (Noise.Next() < 0.6 / Rate * Kmh / 30 * Water) this.Splash = Noise.Range(0.5, 1.2);
                this.Splash *= 1 - 9 / Rate;
                const Splash = this.SplashBand.Run(Noise.White()) * this.Splash * Water * Moving;
                OutL += Splash * 0.35; OutR += Splash * 0.3;
            }

            // Gravel: discrete stone hits (density ∝ speed), crunch bed, and stones pinging the underbody.
            if (Stones > 0.01 && Moving > 0)
            {
                const Density = Stones * (60 + Kmh * 18);
                if (Noise.Next() < Density / Rate)
                {
                    const Stone = this.StonePool[this.StoneNext]; this.StoneNext = (this.StoneNext + 1) % this.StonePool.length;
                    Stone.Ring.Tune(Noise.Range(1300, 6500), Noise.Range(0.004, 0.02));
                    Stone.Side = Noise.Next(); Stone.Hit = Noise.Range(0.3, 1) * (0.6 + Level);
                }
                let Hits = [0, 0];
                for (const Stone of this.StonePool)
                {
                    const Ring = Stone.Ring.Run(Stone.Hit); Stone.Hit = 0;
                    Hits[0] += Ring * (1 - Stone.Side); Hits[1] += Ring * Stone.Side;
                }
                if (Noise.Next() < Stones * Kmh * 0.03 / Rate)
                {
                    const Ping = this.Ping[this.PingNext]; this.PingNext = (this.PingNext + 1) % this.Ping.length;
                    Ping.Ring.Tune(Noise.Range(1800, 4200), Noise.Range(0.08, 0.3)); Ping.Side = Noise.Next(); Ping.Hit = Noise.Range(1, 2.2);
                }
                for (const Ping of this.Ping) { const Ring = Ping.Ring.Run(Ping.Hit); Ping.Hit = 0; Hits[0] += Ring * (1 - Ping.Side) * 0.6; Hits[1] += Ring * Ping.Side * 0.6; }
                this.CrunchEnv += ((0.4 + Noise.Next()) - this.CrunchEnv) * 0.002;
                OutL += Hits[0] * 1.8 * Stones + this.Crunch[0].Run(Noise.White()) * this.CrunchEnv * Stones * Level * 0.9;
                OutR += Hits[1] * 1.8 * Stones + this.Crunch[1].Run(Noise.White()) * this.CrunchEnv * Stones * Level * 0.9;
            }

            // Thumps: periodic (cobbles, strip) or random (dirt, grass) impacts into the suspension and body modes.
            let Strike = 0;
            if (Spacing > 0 && this.Distance - this.LastStone >= Spacing * (Spaced > 0.5 ? 1 : 1e9))
            {
                this.LastStone = this.Distance + (Noise.Next() - 0.5) * Spacing * (Thump > 1.2 ? 0.02 : 0.35);
                Strike = (Thump > 1.2 ? 1 : Noise.Range(0.45, 1.1)) * Moving;
            }
            else if (Spacing === 0 && Thump > 0.01 && Noise.Next() < Thump * (4 + Kmh * 0.25) / Rate) Strike = Noise.Range(0.2, 1) * Thump * Moving;
            let Body = 0;
            if (Strike || this.Impact)
            {
                this.Impact = Strike;
                Body = this.Body[0].Run(this.Impact) * 1.0 + this.Body[1].Run(this.Impact) * 0.8 + this.Body[2].Run(this.Impact * Noise.Range(0.5, 1)) * 0.45;
            }
            else Body = this.Body[0].Run(0) + this.Body[1].Run(0) * 0.8 + this.Body[2].Run(0) * 0.45;
            const Bump = this.Bump.Run(Strike * 0.4) * Thump * Moving;
            const Knock = (Body * 6 + Bump * 3) * Math.min(1, 0.45 + Kmh / 80);
            OutL += Knock; OutR += Knock;
            OutL = OutL * this.RoadLevel + WindL * this.WindLevel;
            OutR = OutR * this.RoadLevel + WindR * this.WindLevel;

            if (this.Pickup === 'Cabin')
            {
                OutL = this.CabinBoom[0].Run(this.Cabin[0].Run(OutL));
                OutR = this.CabinBoom[1].Run(this.Cabin[1].Run(OutR));
            }
            Left[Index] += SoftClip(OutL * 0.5 * Loudness, 1.4);
            Right[Index] += SoftClip(OutR * 0.5 * Loudness, 1.4);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SQUEAL
//------------------------------------------------------------------------------------------------------------------------

/**
 * Tyre squeal, skid and wheelspin (Follow-up 22). Feed: { Speed m/s, Corner 0–1 (lateral slip), LockUp 0–1 (braked
 * slide), Wheelspin 0–1 (driven slip), Grip 0–1, Surface (a Surfaces name) }.
 * The squeal is the rubber's voice, not a scrub: tread blocks stick and slip hundreds of times a second in step,
 * so each sliding tyre sings a strong, nearly pure tone (about 700–1400 Hz, rising with slip) with a few overtones,
 * coloured by the tyre's belt and sidewall resonances (fixed peaks near 1.1 and 2.6 kHz). The stick-slip runs in
 * clusters, so the tone warbles ("eee-ee-eee", 5–12 Hz) and drifts; each tyre slides at its own slip, so three voices
 * (front pair and the rears) sit a little apart in pitch and beat against each other, taking turns to lead. Through a
 * drift it is a sustained wail that bends with the angle; at the grip limit it chirps on and off. The surface sets how
 * much it sings (Surface.Squeal: Tone, Pitch): smooth garage concrete shrieks high, asphalt wails, wet asphalt only
 * chirps over hiss, cobbles and rumble strips chop it, loose surfaces don't squeal at all (spray instead). Lock-up adds
 * a lower, rougher scrub and pulls the pitch down; wheelspin adds a smoke-hiss bed and a howl that climbs with slip.
 */
class Singer
{
    constructor(Rate, Seed)
    {
        this.Tones = [[1, 1, 0.1], [2.008, 0.42, 0.16], [3.015, 0.2, 0.22], [4.03, 0.09, 0.3]].map(([Ratio, Level, Breath], Index) =>
            [new NarrowTone(Rate, { Breath, Width: 0.004 + Index * 0.002, Jitter: 0.003, Flutter: 0.08, Seed: Seed + Index }), Ratio, Level]);
        this.Warble = new Wander(Rate, 8, Seed + 7); this.Drift = new Wander(Rate, 0.9, Seed + 8); this.Share = new Wander(Rate, 0.5, Seed + 9);
    }

    Run(Pitch, Rate)
    {
        const Warble = this.Warble.Run(), Hz = Pitch * (1 + 0.012 * Warble + 0.035 * this.Drift.Run());
        let Out = 0;
        for (const [Tone, Ratio, Level] of this.Tones) { const Partial = Hz * Ratio; if (Partial < Rate * 0.42) Out += Tone.Run(Partial) * Level; }
        return Out * (0.72 + 0.28 * Warble) * Clamp(0.65 + 0.6 * this.Share.Run(), 0.15, 1.2);
    }
}

export class TyreSqueal
{
    constructor(Rate, { Seed = 31, Pickup = 'Chase' } = {})
    {
        this.Rate = Rate; this.Pickup = Pickup;
        this.Noise = new SeededNoise(Seed);
        this.Speed = new Slew(Rate, 0.05, 0);
        this.Corner = new Slew(Rate, 0.06, 0); this.LockUp = new Slew(Rate, 0.03, 0); this.Wheelspin = new Slew(Rate, 0.05, 0);
        this.Grip = new Slew(Rate, 0.2, 1); this.Tone = new Slew(Rate, 0.25, 1); this.Pitch = new Slew(Rate, 0.25, 1);
        this.Singers = [0, 1, 2].map((Index) => new Singer(Rate, Seed + 20 + Index * 13));
        this.Spread = [[1, -0.55], [1.055, 0.55], [0.94, 0.1]];        // pitch offset and pan: front left, front right, rears
        this.Body = [0, 1].map(() => [new Biquad(Rate).Shape('Peak', 1100, 1.4, 5), new Biquad(Rate).Shape('Peak', 2600, 2, 4), new Biquad(Rate).Shape('High', 450, 0.7)]);
        this.Catch = new Wander(Rate, 6, Seed + 5); this.Chop = 0; this.Spacing = 0;
        this.Scrub = [new Biquad(Rate).Shape('Band', 520, 1.2), new Biquad(Rate).Shape('Band', 560, 1.2)];
        this.Smoke = [new Biquad(Rate).Shape('High', 1500, 0.6), new Biquad(Rate).Shape('High', 1500, 0.6)];
        this.Loose = [new Biquad(Rate).Shape('Band', 1100, 0.5), new Biquad(Rate).Shape('Band', 1200, 0.5)];
        this.Chatter = 0;
        this.Cabin = [new Biquad(Rate).Shape('HighShelf', 1200, 0.7, -9), new Biquad(Rate).Shape('HighShelf', 1200, 0.7, -9)];
    }

    Feed({ Speed = 0, Corner = 0, LockUp = 0, Wheelspin = 0, Grip = 1, Surface = null })
    {
        this.Speed.Target = Speed; this.Corner.Target = Clamp(Corner, 0, 1); this.LockUp.Target = Clamp(LockUp, 0, 1);
        this.Wheelspin.Target = Clamp(Wheelspin, 0, 1); this.Grip.Target = Clamp(Grip, 0, 1);
        const Recipe = Surface && Surfaces[Surface] ? Surfaces[Surface] : null;
        const Sings = Recipe ? Recipe.Squeal : { Tone: Smoothstep(0.55, 0.9, Grip), Pitch: 1 };
        this.Tone.Target = Sings.Tone; this.Pitch.Target = Sings.Pitch; this.Spacing = Recipe ? Recipe.Spacing : 0;
    }

    Render(Left, Right, Offset = 0, Count = Left.length - Offset)
    {
        const Rate = this.Rate, Noise = this.Noise;
        for (let Index = Offset; Index < Offset + Count; Index++)
        {
            const Speed = this.Speed.Run(), Corner = this.Corner.Run(), LockUp = this.LockUp.Run(), Spin = this.Wheelspin.Run(), Grip = this.Grip.Run();
            const Tone = this.Tone.Run(), Scale = this.Pitch.Run();
            const Slide = Math.max(Corner, LockUp);
            const Drive = Math.max(Slide * Smoothstep(1, 6, Speed), Spin);
            if (Drive < 1e-4 && this.Chatter < 1e-4) { this.Chatter = 0; continue; }
            const Sticky = Smoothstep(0.55, 0.9, Grip);

            // Pitch: up with slip; lock-up lower; wheelspin climbs with the slip speed.
            const SlidePitch = Mix(720, 1250, Smoothstep(0.15, 1, Drive)) * (1 - 0.2 * LockUp);
            const SpinPitch = Mix(620, 1500, Smoothstep(0, 1, Spin));
            const Pitch = (Spin > Slide ? Mix(SlidePitch, SpinPitch, Smoothstep(0.1, 0.6, Spin)) : SlidePitch) * Scale;

            // Onset: it starts singing once the slip passes the grip peak; near the limit it catches and lets go (chirps).
            const Catch = this.Catch.Run();
            const Sing = Smoothstep(0.12, 0.45, Drive) * Clamp(1.4 * Smoothstep(0.2, 0.7, Drive) + 0.5 * Catch, 0, 1);
            this.Chatter += (Sing - this.Chatter) * 0.003;
            // Cobbles and rumble strips: the contact breaks at every stone or strip, chopping the tone.
            let Chop = 1;
            if (this.Spacing > 0 && Speed > 0.5) { this.Chop += Speed / this.Spacing / Rate; this.Chop -= Math.floor(this.Chop); Chop = 0.35 + 0.65 * Smoothstep(0.15, 0.4, this.Chop); }
            const Level = this.Chatter * Tone * Chop;
            let SingL = 0, SingR = 0;
            if (Level > 1e-4)
                this.Singers.forEach((Singer, Slot) =>
                {
                    const [Ratio, Pan] = this.Spread[Slot], Out = Singer.Run(Pitch * Ratio, Rate);
                    SingL += Out * (1 - Pan) * 0.5; SingR += Out * (1 + Pan) * 0.5;
                });
            const [BodyL, BodyR] = this.Body;
            SingL = BodyL[2].Run(BodyL[1].Run(BodyL[0].Run(SingL))) * Level; SingR = BodyR[2].Run(BodyR[1].Run(BodyR[0].Run(SingR))) * Level;

            // Scrub (lock-up), smoke hiss (wheelspin), loose-surface spray.
            const Fast = Catch;
            const Scrub = LockUp * Smoothstep(1, 8, Speed);
            const ScrubL = this.Scrub[0].Run(Noise.White()) * Scrub * (0.7 + 0.3 * Fast), ScrubR = this.Scrub[1].Run(Noise.White()) * Scrub * (0.7 + 0.3 * Fast);
            const Smoke = Spin * (0.4 + 0.6 * Sticky);
            const SmokeL = this.Smoke[0].Run(Noise.White()) * Smoke, SmokeR = this.Smoke[1].Run(Noise.White()) * Smoke;
            const LooseL = this.Loose[0].Run(Noise.White()) * Drive * (1 - Sticky), LooseR = this.Loose[1].Run(Noise.White()) * Drive * (1 - Sticky);

            let OutL = SingL * 0.75 + ScrubL * 1.6 + SmokeL * 0.3 + LooseL * 2.6;
            let OutR = SingR * 0.75 + ScrubR * 1.6 + SmokeR * 0.3 + LooseR * 2.6;
            if (this.Pickup === 'Cabin') { OutL = this.Cabin[0].Run(OutL); OutR = this.Cabin[1].Run(OutR); }
            Left[Index] += SoftClip(OutL * 0.6, 1.3);
            Right[Index] += SoftClip(OutR * 0.6, 1.3);
        }
    }
}
