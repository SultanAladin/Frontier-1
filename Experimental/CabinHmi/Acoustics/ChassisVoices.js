// CHASSIS VOICES
// 🧩 Live voices for the running gear: disc brakes (pad friction through rotor modes, low-speed squeal, creep groan at a
//    stop, ABS pressure pulsing with pump buzz and valve clicks) and suspension (a quarter-car spring, damper and tyre
//    model driven by the road surface, kerb or pothole kicks and weight transfer: damper hiss, transmitted thuds,
//    bump-stop and top-out clunks, bushing creaks and interior rattles).
// 📝 Same contract as the other voices: Feed() each block, Render() adds stereo samples. Both are louder from the cabin
//    pickup than from the chase microphone, as in a real car.

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, OnePole, Biquad, ModeBank, Slew, Wander, PanGains, SoftClip } from './SignalKit.js';
import { Surfaces } from './ContactPatch.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    BRAKES
//------------------------------------------------------------------------------------------------------------------------

/**
 * Disc brakes. Feed: { Speed m/s, Brake 0–1 (pedal), Abs (bool, anti-lock cycling), Handbrake (bool) }.
 * Friction noise grows with pressure × sliding speed and rings the rotor's modes; squeal appears at low speed with light
 * pressure (mode lock-in on one rotor mode); a groan (stick-slip at 70–110 Hz) appears when creeping to a stop; ABS
 * chops the pressure at ~14 Hz with the pump motor buzzing and the valves clicking.
 */
export class BrakeRotor
{
    constructor(Rate, { Seed = 41, Pickup = 'Chase' } = {})
    {
        this.Rate = Rate; this.Pickup = Pickup;
        this.Noise = new SeededNoise(Seed);
        this.Speed = new Slew(Rate, 0.05, 0); this.Pressure = new Slew(Rate, 0.025, 0);
        this.Abs = false; this.AbsPhase = 0; this.AbsLevel = new Slew(Rate, 0.05, 0); this.PumpPhase = 0; this.LastHalf = 0;
        this.Pad = [new Biquad(Rate).Shape('Band', 2200, 0.6), new Biquad(Rate).Shape('Band', 2500, 0.6)];
        this.Rotor = [new ModeBank(Rate, [[1750, 0.14], [3150, 0.12, 0.8], [4800, 0.1, 0.6], [7300, 0.07, 0.45], [10400, 0.05, 0.3]]),
            new ModeBank(Rate, [[1810, 0.14], [3240, 0.12, 0.8], [4950, 0.1, 0.6], [7500, 0.07, 0.45], [10700, 0.05, 0.3]])];
        this.Squeal = new Slew(Rate, 0.12, 0); this.SquealPhase = 0; this.Flicker = new Wander(Rate, 2.5, Seed + 1); this.Drift = new Wander(Rate, 0.7, Seed + 2);
        this.Groan = new Slew(Rate, 0.08, 0); this.GroanPhase = 0;
        this.GroanBody = new ModeBank(Rate, [[95, 0.05], [210, 0.04, 0.7], [480, 0.03, 0.4]]);
        this.Valve = new ModeBank(Rate, [[2400, 0.006], [4100, 0.004, 0.6]]);
        this.PumpTone = new Biquad(Rate).Shape('Peak', 420, 1.2, 8);
        this.Cabin = [new Biquad(Rate).Shape('HighShelf', 2500, 0.7, -8), new Biquad(Rate).Shape('HighShelf', 2500, 0.7, -8)];
    }

    Feed({ Speed = 0, Brake = 0, Abs = false, Handbrake = false })
    {
        this.Speed.Target = Speed;
        this.Pressure.Target = Clamp(Math.max(Brake, Handbrake ? 0.45 : 0), 0, 1);
        this.Abs = Abs;
    }

    Render(Left, Right, Offset = 0, Count = Left.length - Offset)
    {
        const Rate = this.Rate, Noise = this.Noise, Cabin = this.Pickup === 'Cabin';
        for (let Index = Offset; Index < Offset + Count; Index++)
        {
            const Speed = this.Speed.Run();
            let Pressure = this.Pressure.Run();
            this.AbsLevel.Target = this.Abs ? 1 : 0;
            const Abs = this.AbsLevel.Run();
            let Click = 0, Pump = 0;
            if (Abs > 0.01)
            {
                // Apply / hold / release cycle at ~14 Hz; a valve click at each change; the pump motor buzzes underneath.
                this.AbsPhase = (this.AbsPhase + 14 / Rate) % 1;
                const Half = this.AbsPhase < 0.55 ? 1 : 0;
                if (Half !== this.LastHalf) { Click = Noise.Range(0.6, 1); this.LastHalf = Half; }
                Pressure *= Mix(1, Half ? 1 : 0.35, Abs);
                this.PumpPhase = (this.PumpPhase + 58 / Rate) % 1;
                const Saw = this.PumpPhase * 2 - 1;
                Pump = this.PumpTone.Run(Saw * 0.5 + Noise.White() * 0.25) * Abs * 0.06;
            }
            const Moving = Smoothstep(0.05, 1.5, Speed);
            const Sliding = Pressure * Math.sqrt(Math.min(Speed, 60) / 30) * Moving;

            // Pad friction through the rotor modes (two front discs, slightly different).
            const RubL = this.Pad[0].Run(Noise.White()) * Sliding, RubR = this.Pad[1].Run(Noise.White()) * Sliding;
            let OutL = RubL * 0.35 + this.Rotor[0].Run(RubL * 0.01) * 0.25;
            let OutR = RubR * 0.35 + this.Rotor[1].Run(RubR * 0.01) * 0.25;

            // Squeal: light pressure, low speed → one rotor mode locks in and sings, flickering on and off.
            const Ripe = Smoothstep(0.03, 0.12, Pressure) * (1 - Smoothstep(0.45, 0.7, Pressure)) * Smoothstep(0.4, 1.5, Speed) * (1 - Smoothstep(7, 11, Speed));
            this.Squeal.Target = Ripe * Smoothstep(0.05, 0.45, this.Flicker.Run());   // comes and goes, as real squeal does
            const Squeal = this.Squeal.Run();
            if (Squeal > 1e-4)
            {
                this.SquealPhase = (this.SquealPhase + 4800 * (1 + 0.006 * this.Drift.Run()) / Rate) % 1;
                const Tone = Math.sin(Tau * this.SquealPhase) + 0.18 * Math.sin(Tau * this.SquealPhase * 2) + 0.06 * Math.sin(Tau * this.SquealPhase * 3);
                OutL += Tone * Squeal * 0.09; OutR += Tone * Squeal * 0.075;
            }

            // Groan: creeping to a stop under pressure, the pads stick and slip at 70–110 Hz.
            this.Groan.Target = Smoothstep(0.15, 0.35, Pressure) * Smoothstep(0.03, 0.25, Speed) * (1 - Smoothstep(0.8, 1.6, Speed));
            const Groan = this.Groan.Run();
            if (Groan > 1e-4)
            {
                this.GroanPhase += Mix(70, 110, Speed / 1.6) * (1 + Noise.Gauss() * 0.04) / Rate;
                let Slip = 0;
                if (this.GroanPhase >= 1) { this.GroanPhase -= 1; Slip = Noise.Range(0.6, 1); }
                const Body = this.GroanBody.Run(Slip * 0.2) * Groan * 0.5;
                OutL += Body; OutR += Body;
            }

            const Valve = this.Valve.Run(Click * 0.4) * 0.08;
            OutL += Pump + Valve; OutR += Pump + Valve * 0.7;
            if (Cabin) { OutL = this.Cabin[0].Run(OutL) * 1.3; OutR = this.Cabin[1].Run(OutR) * 1.3; }
            Left[Index] += SoftClip(OutL, 1.2);
            Right[Index] += SoftClip(OutR, 1.2);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SUSPENSION
//------------------------------------------------------------------------------------------------------------------------

/** Per-surface road texture for the quarter-car: bumps per metre, height range (m), length range (m), periodic spacing. */
const Roads = {
    Asphalt: { Rate: 0.6, Height: [0.001, 0.004], Length: [0.25, 0.6] },
    Wet: { Rate: 0.6, Height: [0.001, 0.004], Length: [0.25, 0.6] },
    Gravel: { Rate: 9, Height: [0.002, 0.006], Length: [0.05, 0.15] },
    Dirt: { Rate: 1.6, Height: [0.005, 0.03], Length: [0.3, 1.0] },
    Grass: { Rate: 2.5, Height: [0.003, 0.016], Length: [0.3, 0.8] },
    Cobbles: { Spacing: 0.115, Height: [0.004, 0.009], Length: [0.06, 0.1] },
    RumbleStrip: { Spacing: 0.32, Height: [0.008, 0.009], Length: [0.14, 0.15] },
};

/**
 * Suspension and body. Feed: { Speed m/s, Surface, Accel (m/s², longitudinal), Lateral (m/s²), Kick (metres: a one-off
 * bump such as a kerb or pothole; send once) }. A quarter-car (body 420 kg, wheel 45 kg, spring, damper, tyre) is
 * integrated every sample under the road profile; weight transfer from Accel and Lateral loads the springs. Sounds come
 * from what the parts do: damper oil flow (hiss ∝ shaft speed), the force fed into the body (thud through body modes),
 * travel limits (bump-stop thud, top-out clunk), bushing stick-slip (creaks) and loose trim (rattles).
 */
export class SuspensionTravel
{
    constructor(Rate, { Seed = 51, Pickup = 'Chase' } = {})
    {
        this.Rate = Rate; this.Pickup = Pickup;
        this.Noise = new SeededNoise(Seed);
        this.Speed = 0; this.Surface = 'Asphalt'; this.Accel = 0; this.Lateral = 0;
        this.Body = { Z: 0, V: 0 }; this.Wheel = { Z: 0, V: 0 };
        this.Bumps = []; this.Distance = 0; this.NextBump = 0;
        this.Transfer = new Slew(Rate, 0.35, 0);
        this.LastForce = 0; this.Stopped = false; this.ToppedOut = false;
        this.Damper = [new Biquad(Rate).Shape('Band', 900, 0.7), new Biquad(Rate).Shape('Band', 1100, 0.7)];
        this.Thud = new ModeBank(Rate, [[48, 0.16], [92, 0.11, 0.8], [175, 0.07, 0.6], [340, 0.04, 0.35]]);
        this.ForceSmooth = new OnePole(Rate, 260);
        this.Stop = new ModeBank(Rate, [[62, 0.09], [150, 0.05, 0.7], [410, 0.025, 0.5], [1300, 0.01, 0.3]]);
        this.Clunk = new ModeBank(Rate, [[230, 0.04], [760, 0.025, 0.6], [2100, 0.01, 0.4]]);
        this.Bushing = { Twist: 0, Squeak: 0, Phase: 0, Pitch: 420 };
        this.BushingBand = new Biquad(Rate).Shape('Band', 900, 3);
        this.Rattles = new ModeBank(Rate, [[2700, 0.008], [3900, 0.006, 0.7], [5600, 0.004, 0.5]]);
        this.Cabin = [0, 1].map(() => [new Biquad(Rate).Shape('HighShelf', 3000, 0.7, -6), new Biquad(Rate).Shape('LowShelf', 120, 0.7, 4)]);
        this.StopHit = 0; this.ClunkHit = 0; this.KickTimer = -1; this.KickLevel = 0;
    }

    Feed({ Speed = 0, Surface = this.Surface, Accel = 0, Lateral = 0, Kick = 0 })
    {
        this.Speed = Math.max(0, Speed); this.Surface = Roads[Surface] ? Surface : 'Asphalt';
        this.Accel = Accel; this.Lateral = Lateral;
        if (Kick)
        {
            const Length = Kick > 0 ? 0.25 : 0.35;                                              // kerb up, pothole down
            this.Bumps.push({ At: this.Distance + 0.02, Height: Kick, Length });
            // The sharp edge (kerb face, far lip of the pothole) slams the tyre and the bump stop: an extra hard strike.
            this.KickTimer = Math.round((0.02 + Length * (Kick > 0 ? 0.25 : 0.85)) / Math.max(this.Speed, 1) * this.Rate);
            this.KickLevel = Clamp(Math.abs(Kick) * 11, 0.3, 1);
        }
    }

    /** Road height under the tyre at the current distance (sum of the raised-cosine bumps passing under it). */
    RoadHeight()
    {
        let Height = 0;
        for (const Bump of this.Bumps)
        {
            const Into = (this.Distance - Bump.At) / Bump.Length;
            if (Into > 0 && Into < 1) Height += Bump.Height * 0.5 * (1 - Math.cos(Tau * Into));
        }
        return Height;
    }

    Render(Left, Right, Offset = 0, Count = Left.length - Offset)
    {
        const Rate = this.Rate, Noise = this.Noise, Road = Roads[this.Surface], Step = 1 / Rate;
        const BodyMass = 420, WheelMass = 45, Spring = 32000, Damping = 2900, Tyre = 260000;
        this.Transfer.Target = Clamp(-this.Accel * 0.0045 + Math.abs(this.Lateral) * 0.003, -0.05, 0.05);
        // Retire bumps already behind the tyre, plan new ones ahead.
        this.Bumps = this.Bumps.filter((Bump) => this.Distance - Bump.At < Bump.Length + 0.01);
        const Ahead = this.Distance + this.Speed * Count / Rate + 0.5;
        while (this.Speed > 0.3 && this.NextBump < Ahead)
        {
            const Gap = Road.Spacing ? Road.Spacing * (1 + (Noise.Next() - 0.5) * 0.1) : -Math.log(1 - Noise.Next() * 0.999) / Road.Rate;
            this.NextBump = Math.max(this.NextBump, this.Distance) + Gap;
            this.Bumps.push({ At: this.NextBump, Height: Noise.Range(...Road.Height) * (Road.Spacing ? 1 : Math.sign(Noise.Next() - 0.25)), Length: Noise.Range(...Road.Length) });
        }
        for (let Index = Offset; Index < Offset + Count; Index++)
        {
            this.Distance += this.Speed * Step;
            const Transfer = this.Transfer.Run();
            const Ground = this.RoadHeight();
            const Body = this.Body, Wheel = this.Wheel;
            const Travel = Wheel.Z - Body.Z + Transfer;              // positive = compressed
            const Shaft = Wheel.V - Body.V;
            // Forces are measured from static ride height: the spring carries the body's weight as a preload, the tyre
            // carries both, so at rest everything balances and the tyre only leaves the road over a real drop.
            let Force = Spring * Travel + Damping * Shaft;
            // Travel limits: rubber bump stop in compression, top-out on extension.
            if (Travel > 0.06) Force += (Travel - 0.06) * 400000;
            if (Travel < -0.075) Force += (Travel + 0.075) * 300000;
            const Contact = Math.max(0, Tyre * (Ground - Wheel.Z) + (BodyMass + WheelMass) * 9.81) - (BodyMass + WheelMass) * 9.81;
            Wheel.V += (Contact - Force) / WheelMass * Step;
            Body.V += Force / BodyMass * Step;
            Wheel.V *= 0.9999; Body.V *= 0.99995;
            Wheel.Z += Wheel.V * Step; Body.Z += Body.V * Step;

            // Damper: oil through the valves, hiss rising with shaft speed.
            const Oil = Math.min(1, Math.abs(Shaft) / 0.6);
            let OutL = this.Damper[0].Run(Noise.White()) * Oil * 0.09, OutR = this.Damper[1].Run(Noise.White()) * Oil * 0.09;

            // Force fed into the body: its changes thump through the body modes.
            const Change = this.ForceSmooth.Run(Force - this.LastForce); this.LastForce = Force;
            const Thud = this.Thud.Run(Change * 0.00005) * 0.5;
            OutL += Thud; OutR += Thud;

            // Bump stop and top-out clunks on entry.
            if (Travel > 0.06 && !this.Stopped) { this.Stopped = true; this.StopHit = Clamp(Shaft / 1.2, 0.2, 1); }
            if (Travel < 0.05) this.Stopped = false;
            if (Travel < -0.075 && !this.ToppedOut) { this.ToppedOut = true; this.ClunkHit = Clamp(-Shaft / 1.2, 0.2, 1); }
            if (Travel > -0.06) this.ToppedOut = false;
            let Slam = 0;
            if (this.KickTimer >= 0 && this.KickTimer-- === 0) { this.StopHit = Math.max(this.StopHit, this.KickLevel); Slam = this.KickLevel; }
            const Stop = this.Stop.Run(this.StopHit * 0.5) * 0.45; this.StopHit = 0;
            const Clunk = this.Clunk.Run(this.ClunkHit * 0.5) * 0.3; this.ClunkHit = 0;
            OutL += Stop + Clunk; OutR += Stop * 0.8 + Clunk;

            // Bushings: twist builds with slow body motion; past the stiction limit they slip with a short squeak.
            const Bushing = this.Bushing;
            Bushing.Twist += Math.abs(Body.V) * Step * 40;
            if (Bushing.Twist > 1 && Math.abs(Body.V) > 0.02 && Math.abs(Body.V) < 0.4) { Bushing.Twist = 0; Bushing.Squeak = Noise.Range(0.5, 1); Bushing.Pitch = Noise.Range(380, 720); }
            if (Bushing.Squeak > 1e-3)
            {
                Bushing.Phase += Bushing.Pitch * (1 + Noise.Gauss() * 0.02) / Rate;
                const Pulse = Bushing.Phase >= 1 ? (Bushing.Phase -= 1, 1) : 0;
                const Squeak = this.BushingBand.Run(Pulse) * Bushing.Squeak * 0.35;
                Bushing.Squeak *= 1 - 1 / (0.05 * Rate);
                OutL += Squeak; OutR += Squeak * 0.6;
            }

            // Loose trim rattles when big forces arrive.
            const Jolt = Math.abs(Change) * 0.00002;
            if (Slam) this.Shake = Slam;
            this.Shake = (this.Shake ?? 0) * (1 - 1 / (0.25 * Rate));
            const Rattle = this.Rattles.Run(Noise.Next() < Jolt * 0.02 + this.Shake * 0.004 ? Noise.Range(0.3, 1) * 0.15 : 0) * 0.12;
            OutL += Rattle; OutR += Rattle * 0.5;

            if (this.Pickup === 'Cabin') { OutL = this.Cabin[0][1].Run(this.Cabin[0][0].Run(OutL)) * 1.4; OutR = this.Cabin[1][1].Run(this.Cabin[1][0].Run(OutR)) * 1.4; }
            else { OutL *= 0.55; OutR *= 0.55; }
            Left[Index] += SoftClip(OutL, 1.3);
            Right[Index] += SoftClip(OutR, 1.3);
        }
    }
}
