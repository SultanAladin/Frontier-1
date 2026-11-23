// CABIN FOLEY
// 🧩 One-shot and looping car sound cues synthesized from struck modes, small motors and shaped noise: lights (switch,
//    relay, xenon ignition, LED welcome with lamp levelling, indicator relay), and cabin sounds (unlock, butterfly door
//    open and close, seatbelt, chime, start button, gear paddle, wipers, window, climate fan).
// 📝 Each cue in Cues has a Group, a Label, a default length and Draw(Left, Right, Rate, Options) which adds into stereo
//    buffers. Nothing is sampled: every click is a burst into tuned resonators (Strike), every motor a harmonic whine
//    with brush noise and a load envelope (Whir).

import { Tau, Clamp, Mix, Smoothstep, SeededNoise, Biquad, Resonator, Wander, PanGains, Strike } from './SignalKit.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    PIECES
//------------------------------------------------------------------------------------------------------------------------

/** Small DC motor: harmonic whine (commutator), brush noise, start surge and load sag. Hz may slide to EndHz. */
function Whir(Left, Right, Rate, Time, Seconds, { Hz = 180, EndHz = Hz, Gain = 0.1, Pan = 0, Brush = 0.4, Seed = 3, Load = null } = {})
{
    const Noise = new SeededNoise(Seed), Start = Math.round(Time * Rate), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    const Brushes = new Biquad(Rate).Shape('Band', 3200, 0.8), Housing = new Biquad(Rate).Shape('Peak', 900, 1.5, 6);
    const [GainL, GainR] = PanGains(Pan);
    let Phase = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Index / Count, Age = Index / Rate;
        const Envelope = Smoothstep(0, 0.03, Age) * (1 - Smoothstep(Seconds - 0.05, Seconds, Age));
        const Strain = Load ? Load(Progress) : 0;
        const Pitch = Mix(Hz, EndHz, Progress) * (1 - 0.25 * Strain) * (Age < 0.06 ? 0.6 + Age / 0.15 : 1);
        Phase += Pitch / Rate;
        const Tone = Math.sin(Tau * Phase) * 0.5 + Math.sin(Tau * Phase * 2) * 0.35 + Math.sin(Tau * Phase * 3) * 0.2 + Math.sin(Tau * Phase * 6) * 0.12;
        const Out = Housing.Run(Tone + Brushes.Run(Noise.White()) * Brush) * Envelope * Gain * (1 + 0.3 * Strain);
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/** Tonal chime with inharmonic partials (struck bar), used by the belt reminder and start-up bong. */
function Chime(Left, Right, Rate, Time, { Hz = 880, Decay = 0.9, Gain = 0.18, Pan = 0 } = {})
{
    const Start = Math.round(Time * Rate), Count = Math.min(Left.length - Start, Math.round(Decay * 1.4 * Rate));
    const Partials = [[1, 1, 1], [2.0, 0.35, 0.6], [2.76, 0.22, 0.35], [5.4, 0.08, 0.2]];
    const [GainL, GainR] = PanGains(Pan);
    for (let Index = 0; Index < Count; Index++)
    {
        const Age = Index / Rate;
        let Out = 0;
        for (const [Ratio, Level, Life] of Partials) Out += Math.sin(Tau * Hz * Ratio * Age) * Level * Math.exp(-Age / (Decay * Life / 3));
        Out *= Gain * Math.min(1, Age / 0.004);
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

/** Band-passed noise swell (air, rubber, webbing) between two times with attack and release. */
function Swish(Left, Right, Rate, Time, Seconds, { Low = 400, High = 3000, Gain = 0.1, Pan = 0, Seed = 5, Shape = null } = {})
{
    const Noise = new SeededNoise(Seed), Start = Math.round(Time * Rate), Count = Math.min(Left.length - Start, Math.round(Seconds * Rate));
    const HighPass = new Biquad(Rate).Shape('High', Low, 0.7), LowPass = new Biquad(Rate).Shape('Low', High, 0.7);
    const [GainL, GainR] = PanGains(Pan);
    for (let Index = 0; Index < Count; Index++)
    {
        const Progress = Index / Count;
        const Envelope = Shape ? Shape(Progress) : Math.sin(Math.PI * Progress);
        const Out = LowPass.Run(HighPass.Run(Noise.White())) * Envelope * Gain;
        Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
    }
}

// Mode sets for recurring materials: [Hz, decay seconds, gain].
const Plastic = [[2300, 0.012, 1], [3900, 0.008, 0.6], [6400, 0.004, 0.35], [420, 0.025, 0.4]];
const Metal = [[1750, 0.035, 0.8], [3150, 0.025, 0.6], [5200, 0.015, 0.4], [7900, 0.008, 0.2]];
const DoorShell = [[58, 0.16, 1.2], [92, 0.12, 0.9], [168, 0.09, 0.6], [260, 0.05, 0.35], [610, 0.025, 0.2]];

//------------------------------------------------------------------------------------------------------------------------
//                                                    CUES
//------------------------------------------------------------------------------------------------------------------------

export const Cues = {
    // ── Lights ──────────────────────────────────────────────────────────────────────────────────────────────────────
    LampSwitch: {
        Group: 'Lights', Label: 'Headlamp switch', Seconds: 0.6,
        Draw(Left, Right, Rate)
        {
            // Rotary switch: two detents (parking, then dipped beam), each a plastic click with a spring snap.
            Strike(Left, Right, Rate, 0.02, { Modes: Plastic, Burst: 0.0008, Gain: 0.9, Pan: -0.3, Seed: 2 });
            Strike(Left, Right, Rate, 0.025, { Modes: [[5200, 0.003, 1]], Burst: 0.0003, Gain: 0.3, Pan: -0.3, Seed: 3 });
            Strike(Left, Right, Rate, 0.21, { Modes: Plastic.map(([Hz, D, G]) => [Hz * 0.94, D, G]), Burst: 0.0008, Gain: 1, Pan: -0.3, Seed: 4 });
        },
    },
    HeadlampRelay: {
        Group: 'Lights', Label: 'Lamp relay', Seconds: 0.5,
        Draw(Left, Right, Rate)
        {
            // Relay in the front fuse box: armature slaps the pole piece (metal), brief coil buzz as contacts settle.
            Strike(Left, Right, Rate, 0.02, { Modes: Metal, Burst: 0.0004, Gain: 0.7, Pan: 0.1, Seed: 6, Breath: { Low: 2000, High: 9000, Decay: 0.01, Gain: 0.15 } });
            Strike(Left, Right, Rate, 0.026, { Modes: [[240, 0.03, 0.6]], Burst: 0.0015, Gain: 0.6, Pan: 0.1, Seed: 7 });
            Whir(Left, Right, Rate, 0.02, 0.035, { Hz: 100, Gain: 0.025, Pan: 0.1, Brush: 0.1, Seed: 8 });
        },
    },
    XenonIgnite: {
        Group: 'Lights', Label: 'Xenon ignition', Seconds: 2.6,
        Draw(Left, Right, Rate)
        {
            // Igniter snap (≈ 23 kV strike), arc flutter while the gas warms, ballast whine falling into a steady hiss,
            // and the bi-xenon shutter solenoid clacking.
            const Noise = new SeededNoise(9);
            for (const [Time, Pan] of [[0.08, -0.6], [0.095, 0.6]])
                Strike(Left, Right, Rate, Time, { Modes: [[7200, 0.003, 1], [11000, 0.002, 0.7]], Burst: 0.0012, Gain: 0.8, Pan, Seed: 10 + Time * 100, Breath: { Low: 3000, High: 14000, Decay: 0.015, Gain: 0.5 } });
            const Start = Math.round(0.1 * Rate), Count = Math.min(Left.length - Start, Math.round(2.4 * Rate));
            const Arc = new Biquad(Rate).Shape('Band', 2400, 1.5);
            let Phase = 0;
            for (let Index = 0; Index < Count; Index++)
            {
                const Age = Index / Rate;
                const Warm = Math.exp(-Age / 0.6);
                const Flutter = (0.5 + 0.5 * Math.sin(Tau * 100 * Age + Noise.White() * 0.8)) * Warm;
                Phase += Mix(9500, 6800, Smoothstep(0, 1.2, Age)) / Rate;
                const Whine = Math.sin(Tau * Phase) * (0.006 * Warm + 0.0018);
                const Out = Arc.Run(Noise.White()) * Flutter * 0.05 + Whine + Noise.White() * 0.0015 * Smoothstep(0, 0.4, Age);
                Left[Start + Index] += Out * 0.8; Right[Start + Index] += Out * 0.8;
            }
            for (const [Time, Pan] of [[0.62, -0.6], [0.66, 0.6]])
                Strike(Left, Right, Rate, Time, { Modes: [[980, 0.02, 0.8], [2600, 0.012, 0.5], [190, 0.03, 0.4]], Burst: 0.0006, Gain: 0.6, Pan, Seed: 14 + Time * 100 });
        },
    },
    LedWelcome: {
        Group: 'Lights', Label: 'LED welcome and levelling', Seconds: 2.4,
        Draw(Left, Right, Rate)
        {
            // Sequential daytime light segments tick on (driver chips), the lamp self-levelling motors sweep down and
            // back up, ending with a soft tick as they park.
            for (let Step = 0; Step < 8; Step++)
            {
                const Time = 0.05 + Step * 0.045;
                Strike(Left, Right, Rate, Time, { Modes: [[6200 + Step * 180, 0.006, 0.6], [3100, 0.004, 0.3]], Burst: 0.0002, Gain: 0.18, Pan: -0.7 + Step * 0.2, Seed: 20 + Step });
            }
            const Down = (Progress) => Smoothstep(0.75, 1, Progress);
            Whir(Left, Right, Rate, 0.45, 0.7, { Hz: 420, EndHz: 380, Gain: 0.016, Pan: -0.65, Brush: 0.25, Seed: 30, Load: Down });
            Whir(Left, Right, Rate, 0.47, 0.7, { Hz: 430, EndHz: 392, Gain: 0.016, Pan: 0.65, Brush: 0.25, Seed: 31, Load: Down });
            Whir(Left, Right, Rate, 1.25, 0.6, { Hz: 400, EndHz: 440, Gain: 0.014, Pan: -0.65, Brush: 0.25, Seed: 32 });
            Whir(Left, Right, Rate, 1.27, 0.6, { Hz: 410, EndHz: 450, Gain: 0.014, Pan: 0.65, Brush: 0.25, Seed: 33 });
            Strike(Left, Right, Rate, 1.9, { Modes: [[1300, 0.02, 0.6], [3300, 0.01, 0.3]], Burst: 0.0006, Gain: 0.25, Pan: 0, Seed: 34 });
        },
    },
    Indicator: {
        Group: 'Lights', Label: 'Indicator relay', Seconds: 3.2, Loop: true,
        Draw(Left, Right, Rate, { Seconds = 3.2 } = {})
        {
            // 85 flashes per minute: a bright "tick" when the lamp lights and a duller "tock" when it goes out.
            const Period = 60 / 85;
            for (let Time = 0.02, Beat = 0; Time < Seconds - 0.1; Time += Period / 2, Beat++)
            {
                const On = Beat % 2 === 0;
                Strike(Left, Right, Rate, Time, {
                    Modes: On ? [[2900, 0.016, 1], [4700, 0.01, 0.6], [1250, 0.02, 0.5]] : [[1900, 0.014, 0.9], [3300, 0.008, 0.5], [800, 0.02, 0.6]],
                    Burst: 0.0005, Gain: On ? 0.55 : 0.42, Pan: -0.15, Seed: 40 + Beat,
                });
            }
        },
    },

    // ── Cabin ───────────────────────────────────────────────────────────────────────────────────────────────────────
    Unlock: {
        Group: 'Cabin', Label: 'Unlock and mirrors', Seconds: 2.4,
        Draw(Left, Right, Rate)
        {
            // Door lock actuators (solenoid thunks, staggered), then both mirrors unfold on their motors and latch.
            for (const [Time, Pan, Seed] of [[0.05, -0.8, 50], [0.07, 0.8, 51]])
            {
                Strike(Left, Right, Rate, Time, { Modes: [[140, 0.05, 1], [420, 0.03, 0.6], [1500, 0.012, 0.4]], Burst: 0.002, Gain: 0.9, Pan, Seed });
                Strike(Left, Right, Rate, Time + 0.004, { Modes: Metal, Burst: 0.0004, Gain: 0.3, Pan, Seed: Seed + 10 });
            }
            const Fold = (Progress) => Smoothstep(0.85, 1, Progress);
            Whir(Left, Right, Rate, 0.5, 1.1, { Hz: 230, EndHz: 260, Gain: 0.05, Pan: -0.9, Brush: 0.35, Seed: 52, Load: Fold });
            Whir(Left, Right, Rate, 0.52, 1.1, { Hz: 240, EndHz: 268, Gain: 0.05, Pan: 0.9, Brush: 0.35, Seed: 53, Load: Fold });
            for (const [Time, Pan] of [[1.6, -0.9], [1.62, 0.9]]) Strike(Left, Right, Rate, Time, { Modes: [[620, 0.03, 0.8], [1800, 0.015, 0.4]], Burst: 0.001, Gain: 0.45, Pan, Seed: 54 });
        },
    },
    DoorOpen: {
        Group: 'Cabin', Label: 'Butterfly door open', Seconds: 2.2,
        Draw(Left, Right, Rate)
        {
            // Handle pull, latch release, the seal unsticking with a rubbery pop, then the gas struts hissing as the
            // dihedral door swings up, ending on the stop.
            Strike(Left, Right, Rate, 0.05, { Modes: Plastic, Burst: 0.001, Gain: 0.6, Pan: -0.6, Seed: 60 });
            Strike(Left, Right, Rate, 0.16, { Modes: [...Metal, [300, 0.04, 0.8]], Burst: 0.0008, Gain: 0.8, Pan: -0.6, Seed: 61 });
            Strike(Left, Right, Rate, 0.2, { Modes: [[85, 0.06, 1], [190, 0.04, 0.6]], Burst: 0.006, Gain: 0.7, Pan: -0.6, Seed: 62, Breath: { Low: 300, High: 1600, Decay: 0.08, Gain: 0.25 } });
            Swish(Left, Right, Rate, 0.25, 1.3, { Low: 1800, High: 7000, Gain: 0.05, Pan: -0.6, Seed: 63, Shape: (P) => Smoothstep(0, 0.1, P) * (1 - Smoothstep(0.6, 1, P)) });
            Swish(Left, Right, Rate, 0.25, 1.2, { Low: 120, High: 600, Gain: 0.07, Pan: -0.7, Seed: 64 });
            Strike(Left, Right, Rate, 1.55, { Modes: [[110, 0.08, 1], [330, 0.04, 0.5], [1200, 0.015, 0.3]], Burst: 0.003, Gain: 0.55, Pan: -0.7, Seed: 65 });
        },
    },
    DoorClose: {
        Group: 'Cabin', Label: 'Door close', Seconds: 1.6,
        Draw(Left, Right, Rate)
        {
            // Air pushed ahead of the door, the door skin thud ringing the body, latch catching twice (secondary then
            // primary), a little trim rattle and the cabin pressure "puff" in the ears.
            Swish(Left, Right, Rate, 0.0, 0.32, { Low: 150, High: 1400, Gain: 0.08, Pan: -0.6, Seed: 70, Shape: (P) => Smoothstep(0, 1, P) });
            Strike(Left, Right, Rate, 0.3, { Modes: DoorShell, Burst: 0.008, Gain: 1.4, Pan: -0.5, Seed: 71, Breath: { Low: 60, High: 900, Decay: 0.12, Gain: 0.5 } });
            Strike(Left, Right, Rate, 0.302, { Modes: [...Metal.map(([Hz, D, G]) => [Hz * 0.8, D, G])], Burst: 0.0005, Gain: 0.55, Pan: -0.5, Seed: 72 });
            Strike(Left, Right, Rate, 0.318, { Modes: Metal, Burst: 0.0004, Gain: 0.45, Pan: -0.5, Seed: 73 });
            for (let Rattle = 0; Rattle < 4; Rattle++)
                Strike(Left, Right, Rate, 0.34 + Rattle * 0.018, { Modes: [[2900 + Rattle * 300, 0.006, 1]], Burst: 0.0003, Gain: 0.08 / (Rattle + 1), Pan: -0.3, Seed: 74 + Rattle });
            Swish(Left, Right, Rate, 0.3, 0.25, { Low: 25, High: 90, Gain: 0.4, Pan: 0, Seed: 79, Shape: (P) => Math.exp(-P * 5) * Smoothstep(0, 0.05, P) });
        },
    },
    Seatbelt: {
        Group: 'Cabin', Label: 'Seatbelt', Seconds: 1.4,
        Draw(Left, Right, Rate)
        {
            // Webbing pulled through the D-ring with retractor ratchet ticks, the tongue sliding in and latching.
            Swish(Left, Right, Rate, 0.0, 0.7, { Low: 1500, High: 6500, Gain: 0.06, Pan: 0.6, Seed: 80 });
            for (let Tick = 0; Tick < 14; Tick++)
                Strike(Left, Right, Rate, 0.03 + Tick * 0.045 + (Tick % 3) * 0.004, { Modes: [[3600, 0.004, 1], [1900, 0.006, 0.5]], Burst: 0.0002, Gain: 0.08, Pan: 0.7, Seed: 81 + Tick });
            Swish(Left, Right, Rate, 0.82, 0.12, { Low: 2500, High: 8000, Gain: 0.05, Pan: 0.4, Seed: 96, Shape: (P) => P });
            Strike(Left, Right, Rate, 0.95, { Modes: [[2100, 0.03, 1], [3700, 0.02, 0.7], [5600, 0.012, 0.4], [700, 0.02, 0.5]], Burst: 0.0005, Gain: 0.75, Pan: 0.4, Seed: 97 });
            Strike(Left, Right, Rate, 0.957, { Modes: [[4400, 0.01, 1]], Burst: 0.0003, Gain: 0.3, Pan: 0.4, Seed: 98 });
        },
    },
    BeltChime: {
        Group: 'Cabin', Label: 'Belt reminder chime', Seconds: 3.4,
        Draw(Left, Right, Rate)
        {
            for (let Ring = 0; Ring < 4; Ring++) Chime(Left, Right, Rate, 0.05 + Ring * 0.8, { Hz: 1046, Decay: 0.7, Gain: 0.12 });
        },
    },
    StartButton: {
        Group: 'Cabin', Label: 'Start button and wake chime', Seconds: 1.8,
        Draw(Left, Right, Rate)
        {
            // Steering-wheel start button (firm rubber dome), cluster wake bong, fuel pump priming hum behind the seats.
            Strike(Left, Right, Rate, 0.05, { Modes: [[1600, 0.015, 1], [3400, 0.008, 0.5], [500, 0.02, 0.4]], Burst: 0.0012, Gain: 0.55, Pan: 0, Seed: 100 });
            Chime(Left, Right, Rate, 0.12, { Hz: 659, Decay: 1.2, Gain: 0.1 });
            Chime(Left, Right, Rate, 0.12, { Hz: 988, Decay: 1.0, Gain: 0.06 });
            Whir(Left, Right, Rate, 0.15, 1.5, { Hz: 290, Gain: 0.022, Pan: 0, Brush: 0.6, Seed: 101 });
        },
    },
    GearPaddle: {
        Group: 'Cabin', Label: 'Gear paddle', Seconds: 0.4,
        Draw(Left, Right, Rate)
        {
            // Column-mounted paddle: a sharp hard click with a short metallic ring from the paddle blade.
            Strike(Left, Right, Rate, 0.02, { Modes: [[2600, 0.02, 1], [4300, 0.012, 0.7], [6800, 0.006, 0.4], [900, 0.015, 0.4]], Burst: 0.0004, Gain: 0.7, Pan: 0.3, Seed: 110 });
        },
    },
    Wipers: {
        Group: 'Cabin', Label: 'Wipers', Seconds: 3.4,
        Draw(Left, Right, Rate)
        {
            // Wiper motor through the scuttle with load at each reversal; rubber blade squeaks on the glass.
            const Strokes = (Progress) => Math.pow(Math.abs(Math.sin(Math.PI * Progress * 2)), 8);
            Whir(Left, Right, Rate, 0.05, 3.2, { Hz: 140, Gain: 0.05, Pan: 0, Brush: 0.3, Seed: 120, Load: Strokes });
            for (const [Time, Pan] of [[0.25, -0.6], [1.05, 0.6], [1.85, -0.6], [2.65, 0.6]])
            {
                Swish(Left, Right, Rate, Time, 0.7, { Low: 900, High: 4000, Gain: 0.05, Pan, Seed: 121 + Time * 10 });
                const Noise = new SeededNoise(130 + Time * 10), Start = Math.round((Time + 0.1) * Rate), Count = Math.round(0.45 * Rate);
                const Band = new Biquad(Rate).Shape('Band', 1450, 6);
                const [GainL, GainR] = PanGains(Pan);
                for (let Index = 0; Index < Count && Start + Index < Left.length; Index++)
                {
                    const Squeak = Math.max(0, Math.sin(Tau * 7 * Index / Rate + Noise.White() * 0.1)) * Math.sin(Math.PI * Index / Count);
                    const Out = Band.Run(Noise.White()) * Squeak * 0.12;
                    Left[Start + Index] += Out * GainL; Right[Start + Index] += Out * GainR;
                }
            }
        },
    },
    Window: {
        Group: 'Cabin', Label: 'Window up', Seconds: 3.0,
        Draw(Left, Right, Rate)
        {
            // Regulator motor; pitch sags as the glass enters the top seal, then the seal thunk and motor stall.
            Whir(Left, Right, Rate, 0.05, 2.5, { Hz: 260, EndHz: 240, Gain: 0.06, Pan: -0.7, Brush: 0.4, Seed: 140, Load: (P) => Smoothstep(0.75, 0.97, P) });
            Swish(Left, Right, Rate, 0.05, 2.5, { Low: 1200, High: 5000, Gain: 0.02, Pan: -0.7, Seed: 141 });
            Strike(Left, Right, Rate, 2.5, { Modes: [[160, 0.05, 1], [520, 0.025, 0.5]], Burst: 0.004, Gain: 0.45, Pan: -0.7, Seed: 142 });
        },
    },
    ClimateFan: {
        Group: 'Cabin', Label: 'Climate fan', Seconds: 6, Loop: true,
        Draw(Left, Right, Rate, { Seconds = 6, Level = 0.6 } = {})
        {
            // Blower: blade-rate tone (11 blades), broadband air through the ducts, vents hissing; flap clunk at start.
            const Noise = new SeededNoise(150), Gust = new Wander(Rate, 0.4, 151);
            const Duct = [new Biquad(Rate).Shape('Band', 520, 0.6), new Biquad(Rate).Shape('Band', 560, 0.6)];
            const Vent = [new Biquad(Rate).Shape('High', 2500, 0.7), new Biquad(Rate).Shape('High', 2500, 0.7)];
            const Count = Math.min(Left.length, Math.round(Seconds * Rate));
            let Phase = 0;
            for (let Index = 0; Index < Count; Index++)
            {
                const Age = Index / Rate;
                const Spin = Smoothstep(0, 1.5, Age) * Level * (1 + 0.04 * Gust.Run());
                const Edge = Math.min(1, Age / 0.05, (Count - Index) / Rate / 0.05);
                Phase += 2600 / 60 * 11 * Spin / Rate;
                const Tone = (Math.sin(Tau * Phase) + 0.3 * Math.sin(Tau * Phase * 2)) * 0.01 * Spin;
                const AirL = Duct[0].Run(Noise.Pink()) * 0.35 + Vent[0].Run(Noise.White()) * 0.05;
                const AirR = Duct[1].Run(Noise.Pink()) * 0.35 + Vent[1].Run(Noise.White()) * 0.05;
                Left[Index] += (AirL * Spin * Spin + Tone) * Edge; Right[Index] += (AirR * Spin * Spin + Tone) * Edge;
            }
            Strike(Left, Right, Rate, 0.05, { Modes: [[230, 0.04, 1], [700, 0.02, 0.4]], Burst: 0.002, Gain: 0.3, Pan: 0, Seed: 152 });
        },
    },
};

/** Renders one cue into fresh stereo buffers. */
export function RenderCue(Name, Rate, Options = {})
{
    const Cue = Cues[Name];
    if (!Cue) throw new Error(`Unknown cue ${Name}`);
    const Seconds = Options.Seconds ?? Cue.Seconds;
    const Left = new Float32Array(Math.ceil(Seconds * Rate)), Right = new Float32Array(Left.length);
    Cue.Draw(Left, Right, Rate, { ...Options, Seconds });
    return { Left, Right, Seconds };
}
