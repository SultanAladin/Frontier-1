// RENDER REEL
// 🧩 Offline renderer for the vehicle sound library: scripts each scene (LaFerrari and GT-R NISMO cold start, launch,
//    onboard, fly-by; limiter, tyres, surfaces, brakes, suspension, lights, cabin; explosions, shockwave, EMP, sonic
//    blast, glass and crash; weapons, bullet fly-bys and bullet impacts; ten turbo and supercharger types; the Taycan
//    Turbo S and the Next-Gen racer; electric aircraft) against the same voices the live bench uses, writes MP3s (192 kbps; add --wav for WAV too)
//    to Exhibits/CabinHmi/Audio and spectrogram plates to Exhibits/CabinHmi/Images, and prints a measurement table.
// 📝 Run once: npm install (in this folder, for lamejs). Then: node Experimental/CabinHmi/Acoustics/Offline/RenderReel.mjs
//    [SceneName …] [--wav]. Deterministic (seeded noise).

import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { PulseTrainEngine, LaFerrari, GtrNismo, Demon, Spyder918, AgeraRs, MustangGt, Gt350, MustangGt3, LigierJsP320 } from '../PulseTrainEngine.js';
import { TractionIntegrator } from '../TractionIntegrator.js';
import { SurfaceRoll, TyreSqueal, Surfaces } from '../ContactPatch.js';
import { RenderCue as RenderCabinCue, Cues as CabinCues } from '../CabinFoley.js';
import { RenderCue as RenderImpactCue, Cues as ImpactCues } from '../ImpactFoley.js';
import { RenderCue as RenderBallisticCue, Materials } from '../Ballistics.js';
import { RenderCue as RenderAviationCue, Cues as AviationCues } from '../ElectricAviation.js';
import { RenderCue as RenderFactoryCue, Cues as FactoryCues } from '../FactoryFoley.js';
import { RenderCue as RenderWarCue, Cues as WarCues } from '../WarFoley.js';
import { ElectricIntegrator, DriveUnit, TaycanTurboS, NextGenRacer } from '../ElectricDrive.js';
import { Inducers, InductedProfile, InductionVoice } from '../ForcedInduction.js';
import { BrakeRotor, SuspensionTravel } from '../ChassisVoices.js';
import { OnePole, DelayLine, PanGains, Smoothstep, Mix } from '../SignalKit.js';
import { WriteWav, WriteMp3, DrawSpectrogram, AverageSpectrum, LevelNear, Centroid } from './SpectrumTools.mjs';

const Rate = 44100, Block = 64;
const Root = join(dirname(fileURLToPath(import.meta.url)), '../../../..');
const AudioOut = join(Root, 'Exhibits/CabinHmi/Audio'), ImageOut = join(Root, 'Exhibits/CabinHmi/Images');

//------------------------------------------------------------------------------------------------------------------------
//                                                    RUNNER
//------------------------------------------------------------------------------------------------------------------------

/** Drives a car scene: Plan(Time, Car, Bus) sets inputs each block; the integrator feeds engine, rolling and squeal. */
function DriveScene(Seconds, Pickup, Plan, { Surface = 'Asphalt', Roll = true, Squeal = true, EngineGain = 1, Profile = LaFerrari, Chassis = false, Induction = null, Sport, LagScale = 1, Trace = null, TraceLabel = null, RoadLevel = 1, WindLevel = 1 } = {})
{
    const Count = Math.ceil(Seconds * Rate), Left = new Float32Array(Count), Right = new Float32Array(Count);
    const EngineL = new Float32Array(Block), EngineR = new Float32Array(Block);
    if (Induction) Profile = InductedProfile(Profile, Induction);
    else Induction = Profile.Stock ?? null;                                  // a stock blower (the Demon) keeps its voice
    const Car = Profile.Electric ? new ElectricIntegrator(Profile) : new TractionIntegrator(Profile);
    Car.LagScale = LagScale;
    const Inducer = Induction ? new InductionVoice(Rate, Induction, { Pickup, Redline: Profile.Redline }) : null;
    // Electric cars: the cyan overlay is the motor's strongest electromagnetic order instead of the firing frequency.
    const Lead = Profile.Electric ? Profile.Motors.at(-1) : null, Harmonic = Lead ? Lead.PolePairs * 6 : 0;
    // Superchargers: the overlay is the predicted blower whine (crank rpm × pulley ratio × pockets, or gear teeth).
    const Blower = Induction && Inducers[Induction].Family === 'Supercharger' ? Inducers[Induction] : null;
    const Whine = Blower ? Blower.Ratio * (Blower.GearTeeth ?? Blower.Pockets) / 60 : 0;
    const Bus = {
        Engine: Profile.Electric ? new DriveUnit(Rate, Profile, { Pickup, Sport }) : new PulseTrainEngine(Rate, Profile, { Pickup }),
        Roll: new SurfaceRoll(Rate, { Pickup }), Squeal: new TyreSqueal(Rate, { Pickup }),
        Brakes: Chassis ? new BrakeRotor(Rate, { Pickup }) : null, Springs: Chassis ? new SuspensionTravel(Rate, { Pickup }) : null,
        Surface, Steer: 0, Kick: 0, Override: null, Telemetry: [], Firing: Lead ? Harmonic / 60 : Blower ? Whine : Profile.Cylinders / 120,
        Label: TraceLabel ?? (Lead ? `CYAN DOTS: PREDICTED ${Harmonic}TH ORDER OF THE ${Lead.Axle.toUpperCase()} MOTOR` : Blower ? `CYAN DOTS: PREDICTED BLOWER WHINE (RPM X ${Whine.toFixed(3)})` : null),
    };
    Bus.Roll.RoadLevel = RoadLevel; Bus.Roll.WindLevel = WindLevel;
    for (let Index = 0; Index < Count; Index += Block)
    {
        const Time = Index / Rate, Size = Math.min(Block, Count - Index);
        Plan(Time, Car, Bus);
        const Feed = Car.Step(Size / Rate);
        Bus.Engine.Feed(Bus.Override ?? Feed);
        EngineL.fill(0); EngineR.fill(0);
        Bus.Engine.Render(EngineL, EngineR, 0, Size);
        if (Inducer) { Inducer.Feed(Bus.Override ?? Feed); Inducer.Render(EngineL, EngineR, 0, Size); }
        for (let Step = 0; Step < Size; Step++) { Left[Index + Step] += EngineL[Step] * EngineGain; Right[Index + Step] += EngineR[Step] * EngineGain; }
        if (Roll) { Bus.Roll.Feed({ Speed: Car.Speed, Surface: Bus.Surface }); Bus.Roll.Render(Left, Right, Index, Size); }
        if (Squeal)
        {
            Bus.Squeal.Feed(Bus.SquealOverride ?? { Speed: Car.Speed, Corner: Car.Corner, LockUp: Car.LockUp, Wheelspin: Car.Wheelspin, Grip: Surfaces[Bus.Surface].Grip, Surface: Bus.Surface });
            Bus.Squeal.Render(Left, Right, Index, Size);
        }
        if (Bus.Brakes)
        {
            Bus.Brakes.Feed({ Speed: Car.Speed, Brake: Car.Brake, Abs: Car.AbsActive, Handbrake: Car.Handbrake });
            Bus.Brakes.Render(Left, Right, Index, Size);
            Bus.Springs.Feed({ Speed: Car.Speed, Surface: Bus.Surface, Accel: Car.Accel, Lateral: Car.Lateral, Kick: Bus.Kick });
            Bus.Springs.Render(Left, Right, Index, Size);
            Bus.Kick = 0;
        }
        if (Index % Math.round(Rate * 0.02) < Block) Bus.Telemetry.push({ Time, Rpm: (Bus.Override ?? Feed).Rpm, Kmh: Car.Speed * 3.6, Gear: Car.Gear, Firing: Bus.Firing, Label: Bus.Label, Hz: Trace ? Trace(Car) : undefined });
    }
    return { Left, Right, Telemetry: Bus.Telemetry };
}

/** Joins scenes end to end (Telemetry times shifted so the firing overlay follows each part). */
function Join(Parts)
{
    const Count = Parts.reduce((Sum, Part) => Sum + Part.Left.length, 0), Left = new Float32Array(Count), Right = new Float32Array(Count), Telemetry = [];
    let At = 0;
    for (const Part of Parts)
    {
        Left.set(Part.Left, At); Right.set(Part.Right, At);
        for (const Row of Part.Telemetry ?? []) Telemetry.push({ ...Row, Time: Row.Time + At / Rate });
        At += Part.Left.length;
    }
    return { Left, Right, Telemetry };
}

/** Lays one-shot cues on a timeline: [[Time, CueName, Options], …]. */
function CueScene(Seconds, Timeline)
{
    const Count = Math.ceil(Seconds * Rate), Left = new Float32Array(Count), Right = new Float32Array(Count);
    for (const [Time, Name, Options = {}] of Timeline)
    {
        const Cue = (CabinCues[Name] ? RenderCabinCue : ImpactCues[Name] ? RenderImpactCue : AviationCues[Name] ? RenderAviationCue : FactoryCues[Name] ? RenderFactoryCue : WarCues[Name] ? RenderWarCue : RenderBallisticCue)(Name, Rate, Options), Start = Math.round(Time * Rate);
        for (let Index = 0; Index < Cue.Left.length && Start + Index < Count; Index++) { Left[Start + Index] += Cue.Left[Index]; Right[Start + Index] += Cue.Right[Index]; }
    }
    return { Left, Right };
}

/** Peak-safe loudness match: aim for a target RMS, never let the peak exceed −1 dBFS (soft knee above −6 dBFS). */
function Master(Left, Right, TargetRms = -18)
{
    if (TargetRms === null) return;                 // one-shot impacts arrive peak-normalized; keep their dynamics
    let Sum = 0;
    for (let Index = 0; Index < Left.length; Index++) Sum += (Left[Index] ** 2 + Right[Index] ** 2) / 2;
    const Rms = 10 * Math.log10(Sum / Left.length + 1e-12);
    const Gain = Math.pow(10, (TargetRms - Rms) / 20);
    const Knee = 0.5, Ceiling = 0.89;
    const Limit = (Sample) => { const Size = Math.abs(Sample); if (Size <= Knee) return Sample; return Math.sign(Sample) * (Knee + (Ceiling - Knee) * Math.tanh((Size - Knee) / (Ceiling - Knee))); };
    for (let Index = 0; Index < Left.length; Index++) { Left[Index] = Limit(Left[Index] * Gain); Right[Index] = Limit(Right[Index] * Gain); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENES
//------------------------------------------------------------------------------------------------------------------------

const Scenes = {
    'LaFerrari-ColdStart': {
        Title: 'LAFERRARI COLD START', Note: 'CHASE MIC. STARTER, CATCH, FLARE TO 3000, COLD IDLE, THREE BLIPS IN NEUTRAL',
        Marks: [[0.3, 'START'], [1.4, 'FLARE'], [5, 'BLIP'], [6.6, 'BLIP'], [7.8, 'BLIP']],
        Build: () => DriveScene(10, 'Chase', (Time, Car) =>
        {
            if (Time >= 0.3) Car.Start();
            Car.Throttle = (Time > 5 && Time < 5.22) || (Time > 6.6 && Time < 6.95) ? 1 : (Time > 7.8 && Time < 8.45 ? 0.75 : 0);
        }, { Squeal: false }),
        Rms: -20,
    },
    'LaFerrari-Launch': {
        Title: 'LAFERRARI LAUNCH', Note: 'CHASE MIC. FULL THROTTLE FROM REST, AUTOMATIC UPSHIFTS WITH IGNITION CUT, LIFT AND OVERRUN',
        Marks: [[1, 'LAUNCH'], [10.5, 'LIFT']],
        Build: () => DriveScene(13.5, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = LaFerrari.Idle; }
            if (Time > 0.5 && Car.Gear === 0) Car.Gear = 1;
            Car.Throttle = Time > 1 && Time < 10.5 ? 1 : 0;
        }),
        Rms: -16,
    },
    'LaFerrari-Onboard': {
        Title: 'LAFERRARI ONBOARD', Note: 'CABIN MIC. PULL THROUGH 3-4-5, HARD BRAKING WITH BLIPPED DOWNSHIFTS, CORNER, EXIT',
        Marks: [[0.2, 'PULL'], [7.4, 'BRAKE'], [10.2, 'CORNER'], [11.6, 'EXIT']],
        Build: () => DriveScene(14, 'Cabin', (Time, Car, Bus) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 95 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; }
            const Pull = Time > 0.2 && Time < 7.4;
            Car.Throttle = Pull ? 1 : Time > 11.6 ? 1 : Time > 10.2 ? 0.35 : 0;
            if (Pull && Car.Rpm > 8900) Car.ShiftUp();
            Car.Brake = Time > 7.4 && Time < 10 ? 0.92 : 0;
            if (Time > 7.4 && Time < 10.2 && Car.Gear > 2 && Car.WheelRpm(Car.Gear - 1) < 7600) Car.ShiftDown();
            if (Time > 11.6 && Car.Rpm > 8900) Car.ShiftUp();
            Car.Steer = Time > 10 && Time < 12.6 ? 0.55 * Smoothstep(10, 10.6, Time) : 0;
        }, { Chassis: true }),
        Rms: -17,
    },
    'LaFerrari-Limiter': {
        Title: 'LAFERRARI REV LIMITER', Note: 'CHASE MIC. NEUTRAL, HOLD ON THE 9250 LIMITER, LIFT WITH OVERRUN POPS, TWO SHARP BLIPS',
        Marks: [[0.5, 'FLAT'], [2.6, 'LIFT'], [4.6, 'BLIP'], [5.7, 'BLIP']],
        Build: () => DriveScene(7.5, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = LaFerrari.Idle; }
            Car.Throttle = (Time > 0.5 && Time < 2.6) || (Time > 4.6 && Time < 4.85) || (Time > 5.7 && Time < 5.98) ? 1 : 0;
        }, { Roll: false, Squeal: false }),
        Rms: -18,
    },
    'LaFerrari-FlyBy': {
        Title: 'LAFERRARI FLY-BY', Note: 'TRACKSIDE MIC 12 M FROM THE LINE. 6TH GEAR FLAT OUT, DOPPLER, DISTANCE, AIR ABSORPTION, GROUND BOUNCE',
        Marks: [[4, 'ABEAM']],
        Build: () => FlyBy(8, 4),
        Rms: -18,
    },
    'GtrNismo-ColdStart': {
        Title: 'GT-R NISMO COLD START', Note: 'CHASE MIC. STARTER, CATCH, FLARE, FAST COLD IDLE NEAR 1150, THREE BLIPS WITH TURBO WHISTLE AND FLUTTER',
        Marks: [[0.3, 'START'], [1.3, 'FLARE'], [5, 'BLIP'], [6.6, 'BLIP'], [7.8, 'BLIP']],
        Build: () => DriveScene(10, 'Chase', (Time, Car) =>
        {
            if (Time >= 0.3) Car.Start();
            Car.Throttle = (Time > 5 && Time < 5.3) || (Time > 6.6 && Time < 7.05) ? 1 : (Time > 7.8 && Time < 8.6 ? 0.8 : 0);
        }, { Squeal: false, Profile: GtrNismo }),
        Rms: -20,
    },
    'GtrNismo-Launch': {
        Title: 'GT-R NISMO LAUNCH', Note: 'CHASE MIC. BRAKE-HOLD BOOST BUILD, AWD LAUNCH, 6-SPEED DCT UPSHIFTS, LIFT WITH BLOW-OFF AND FLUTTER',
        Marks: [[0.6, 'BUILD'], [1.8, 'LAUNCH'], [10.6, 'LIFT']],
        Build: () => DriveScene(13.5, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            if (Time > 0.6 && Time < 1.8) { Car.Gear = 0; Car.Throttle = 0.42; }        // launch control holds ~4000 in neutral
            else Car.Throttle = Time >= 1.8 && Time < 10.6 ? 1 : 0;
            if (Time >= 1.8 && Car.Gear === 0) Car.Gear = 1;
        }, { Profile: GtrNismo }),
        Rms: -16,
    },
    'Demon-ColdStart': {
        Title: 'DODGE CHALLENGER SRT DEMON COLD START', Note: 'CHASE MIC. STARTER, CATCH AND FLARE, LUMPY CROSS-PLANE IDLE (EACH BANK FIRES UNEVENLY), THREE BLIPS WITH THE BLOWER SCREAM, OVERRUN POPS',
        Marks: [[0.3, 'START'], [1.3, 'FLARE'], [5, 'BLIP'], [6.6, 'BLIP'], [7.8, 'BLIP']],
        Build: () => DriveScene(10.5, 'Chase', (Time, Car) =>
        {
            if (Time >= 0.3) Car.Start();
            Car.Throttle = (Time > 5 && Time < 5.3) || (Time > 6.6 && Time < 7.05) ? 1 : (Time > 7.8 && Time < 8.6 ? 0.85 : 0);
        }, { Squeal: false, Profile: Demon }),
        Rms: -19,
    },
    'Demon-Launch': {
        Title: 'DODGE CHALLENGER SRT DEMON TRANSBRAKE LAUNCH', Note: 'CHASE MIC. STAGED ON THE TRANSBRAKE AT 2350 RPM, RELEASE, 1.8 G, 8-SPEED UPSHIFTS, QUARTER MILE IN ABOUT 9.8 S AT 142 MPH, LIFT',
        Marks: [[0.8, 'STAGE'], [2.6, 'RELEASE'], [12.5, '1/4 MILE, LIFT']],
        Build: () => DriveScene(15.5, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = Demon.Idle; Car.Automatic = true; }
            if (Time >= 0.8 && Time < 2.6) { Car.Gear = 1; Car.TransBrake = true; Car.Throttle = 1; }   // first + reverse locked: held at 2350
            else if (Time >= 2.6) { Car.TransBrake = false; Car.Throttle = Time < 12.6 ? 1 : 0; }
        }, { Profile: Demon }),
        Rms: -16,
    },
    'Demon-RollOn': {
        Title: 'DEMON ROLL-ON: SUPERCHARGER, NO LAG', Note: 'CHASE MIC. THIRD GEAR FROM 2600 RPM, FLOOR IT THREE TIMES: THE BELT-DRIVEN BLOWER MAKES FULL BOOST THE INSTANT THE THROTTLE OPENS (COMPARE TURBO LAG)',
        Marks: [[1, 'FLOOR'], [6.5, 'FLOOR'], [12, 'FLOOR']],
        Build: () => DriveScene(16.5, 'Chase', (Time, Car) =>
        {
            const Cycle = Time % 5.5;
            if (Cycle < 0.02) { Car.Running = true; Car.Automatic = false; Car.Gear = 3; Car.Rpm = 2600; Car.Speed = 2600 / 60 / Demon.Ratios[2] / Demon.Final * 2 * Math.PI * Demon.TyreRadius; }
            Car.Throttle = Cycle < 1 ? 0.12 : Cycle < 3.6 ? 1 : 0;
        }, { Squeal: false, Profile: Demon }),
        Rms: -17,
    },
    'TurboLag-Compare': {
        Title: 'TURBO LAG: GT-R + TWIN TURBOS, SAME ROLL-ON THREE WAYS', Note: 'CHASE MIC. THIRD GEAR FROM 2600 RPM, FLOOR IT WITH LAG OFF, REAL LAG, THEN 2.5X LAG: THE WHISTLE AND THE SHOVE ARRIVE LATE, THEN BOOST HITS',
        Marks: [[1, 'LAG OFF'], [6.5, 'REAL LAG'], [12, 'LAG X2.5']],
        Build: () => DriveScene(16.5, 'Chase', (Time, Car) =>
        {
            const Cycle = Time % 5.5;
            if (Cycle < 0.02) { Car.Running = true; Car.Automatic = false; Car.Gear = 3; Car.Rpm = 2600; Car.Speed = 2600 / 60 / GtrNismo.Ratios[2] / GtrNismo.Final * 2 * Math.PI * GtrNismo.TyreRadius; Car.Spool = 0.1; Car.Boost = 0; Car.LagScale = [0, 1, 2.5][Math.floor(Time / 5.5)]; }
            Car.Throttle = Cycle < 1 ? 0.12 : Cycle < 3.6 ? 1 : 0;
        }, { Squeal: false, Profile: GtrNismo, Induction: 'TwinTurbo', Trace: (Car) => 4200 + (9000 - 4200) * Car.Spool, TraceLabel: 'CYAN DOTS: TURBO WHISTLE PREDICTED FROM THE SIMULATED SHAFT SPEED' }),
        Rms: -17,
    },
    'Spyder918-Launch': {
        Title: 'PORSCHE 918 SPYDER LAUNCH', Note: 'CHASE MIC. LAUNCH CONTROL, BOTH E-MOTORS WHINING UNDER THE V8, FLAT-PLANE SCREAM TO 9150 RPM THROUGH THE TOP PIPES, PDK UPSHIFTS, FRONT MOTOR DECOUPLES AT 235 KM/H, LIFT',
        Marks: [[0.8, 'LAUNCH CONTROL'], [2, 'GO'], [13, 'LIFT']],
        Build: () => DriveScene(16, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = Spyder918.Idle; Car.Automatic = true; }
            if (Time > 0.8 && Time < 2) { Car.Gear = 0; Car.Throttle = 0.3; }      // launch control holds the revs
            else Car.Throttle = Time >= 2 && Time < 13 ? 1 : 0;
            if (Time >= 2 && Car.Gear === 0) Car.Gear = 1;
        }, { Profile: Spyder918 }),
        Rms: -16,
    },
    'Spyder918-FlyBy': {
        Title: 'PORSCHE 918 SPYDER FLY-BY', Note: 'TRACKSIDE MIC, 12 M FROM THE LINE: 250 KM/H IN 5TH, V8 AT FULL THROTTLE WITH THE HYBRID MOTOR, DOPPLER DROP, TOP PIPES POINTING AWAY',
        Marks: [[4, 'PASS']],
        Build: () => FlyBy(8, 4, Spyder918, 250, 5, { Motor: 0.6 }),
        Rms: -18,
    },
    'AgeraRs-Launch': {
        Title: 'KOENIGSEGG AGERA RS LAUNCH', Note: 'CHASE MIC. BOOST BUILD AT ABOUT 3800 RPM, LAUNCH, TWIN-TURBO CROSS-PLANE V8 (90 DEGREE CRANK) THROUGH 7 GEARS, DUMP-VALVE CHIRPS, LIFT WITH THE BIG BLOW-OFF',
        Marks: [[0.8, 'BUILD'], [2, 'LAUNCH'], [13, 'LIFT']],
        Build: () => DriveScene(16, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = AgeraRs.Idle; Car.Automatic = true; }
            if (Time > 0.8 && Time < 2) { Car.Gear = 0; Car.Throttle = 0.3; }
            else Car.Throttle = Time >= 2 && Time < 13 ? 1 : 0;
            if (Time >= 2 && Car.Gear === 0) Car.Gear = 1;
        }, { Profile: AgeraRs }),
        Rms: -16,
    },
    'AgeraRs-FlyBy': {
        Title: 'KOENIGSEGG AGERA RS FLY-BY', Note: 'TRACKSIDE MIC, 12 M FROM THE LINE: 300 KM/H IN 5TH ON FULL BOOST, TURBO WHISTLE OVER THE V8, DOPPLER DROP',
        Marks: [[4, 'PASS']],
        Build: () => FlyBy(8, 4, AgeraRs, 300, 5, { Boost: 1 }),
        Rms: -18,
    },
    'Road-NoiseSwitch': {
        Title: 'ROAD NOISE: FULL, BENCH DEFAULT, OFF', Note: 'AGERA RS, CHASE MIC, 6TH GEAR FLAT OUT FROM 220 KM/H. ROAD ROAR AT 100 %, THEN THE BENCH DEFAULT (ROAD 40 %, WIND 80 %), THEN ROAD SWITCHED OFF (WIND ONLY)',
        Marks: [[0.1, 'ROAD 100 %'], [5, 'ROAD 40 % WIND 80 %'], [10, 'ROAD OFF']],
        Build: () => DriveScene(15, 'Chase', (Time, Car, Bus) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 6; Car.Speed = 220 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; Car.Boost = 1; }
            Car.Throttle = 1;
            [Bus.Roll.RoadLevel, Bus.Roll.WindLevel] = Time < 5 ? [1, 1] : Time < 10 ? [0.4, 0.8] : [0, 0.8];
        }, { Profile: AgeraRs }),
        Rms: -18,
    },
    'MustangGt-StartLaunch': {
        Title: 'FORD MUSTANG GT START AND LAUNCH', Note: 'CHASE MIC. COYOTE 5.0 CROSS-PLANE V8: STARTER, FLARE, LUMPY IDLE (EACH BANK FIRES UNEVENLY), TWO BLIPS, REVS HELD, LAUNCH THROUGH THE 6-SPEED MANUAL, LIFT WITH OVERRUN CRACKLE',
        Marks: [[0.3, 'START'], [3.6, 'BLIP'], [4.8, 'BLIP'], [6.2, 'HOLD'], [7, 'LAUNCH'], [14, 'LIFT']],
        Build: () => DriveScene(16.5, 'Chase', (Time, Car) =>
        {
            if (Time >= 0.3 && Time < 0.4) Car.Start();
            if (Time < 7) { Car.Gear = 0; Car.Automatic = true; Car.Throttle = (Time > 3.6 && Time < 3.9) || (Time > 4.8 && Time < 5.25) ? 1 : Time > 6.2 ? 0.32 : 0; }
            else Car.Throttle = Time < 14 ? 1 : 0;
            if (Time >= 7 && Car.Gear === 0) Car.Gear = 1;
        }, { Profile: MustangGt }),
        Rms: -17,
    },
    'Gt350-Launch': {
        Title: 'SHELBY GT350 LAUNCH', Note: 'CHASE MIC. 5.2 VOODOO FLAT-PLANE V8: REVS HELD, LAUNCH, SCREAM TO 8250 RPM THROUGH THE TREMEC 6-SPEED, LIFT. NO BURBLE: EACH BANK FIRES EVENLY',
        Marks: [[0.8, 'HOLD'], [2, 'LAUNCH'], [13, 'LIFT']],
        Build: () => DriveScene(16, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = Gt350.Idle; Car.Automatic = true; }
            if (Time > 0.8 && Time < 2) { Car.Gear = 0; Car.Throttle = 0.35; }
            else Car.Throttle = Time >= 2 && Time < 13 ? 1 : 0;
            if (Time >= 2 && Car.Gear === 0) Car.Gear = 1;
        }, { Profile: Gt350 }),
        Rms: -16,
    },
    'MustangGt3-Onboard': {
        Title: 'FORD MUSTANG GT3 ONBOARD (LMGT3)', Note: 'CABIN MIC. 5.4 RACE COYOTE, SIDE EXHAUSTS, XTRAC SEQUENTIAL: PULL 3-4-5 WITH STRAIGHT-CUT WHINE AND CUT SHIFTS, HARD STOP WITH BLIPPED DOWNSHIFTS AND OVERRUN BANGS, CORNER, EXIT',
        Marks: [[0.2, 'PULL'], [7.4, 'BRAKE'], [10.2, 'CORNER'], [11.6, 'EXIT']],
        Build: () => DriveScene(14, 'Cabin', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 110 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; }
            const Pull = Time > 0.2 && Time < 7.4;
            Car.Throttle = Pull ? 1 : Time > 11.6 ? 1 : Time > 10.2 ? 0.3 : 0;
            if (Pull && Car.Rpm > 7500) Car.ShiftUp();
            Car.Brake = Time > 7.4 && Time < 10 ? 1 : 0;
            if (Time > 7.4 && Time < 10.2 && Car.Gear > 2 && Car.WheelRpm(Car.Gear - 1) < 6200) Car.ShiftDown();
            if (Time > 11.6 && Car.Rpm > 7500) Car.ShiftUp();
            Car.Steer = Time > 10 && Time < 12.6 ? 0.5 * Smoothstep(10, 10.6, Time) : 0;
        }, { Profile: MustangGt3, Chassis: true }),
        Rms: -17,
    },
    'MustangGt3-FlyBy': {
        Title: 'FORD MUSTANG GT3 FLY-BY', Note: 'TRACKSIDE MIC, 12 M FROM THE LINE: 265 KM/H IN 6TH AT FULL THROTTLE, CROSS-PLANE BARK FROM THE SIDE PIPES, DOPPLER DROP',
        Marks: [[4, 'PASS']],
        Build: () => FlyBy(8, 4, MustangGt3, 265, 6, {}),
        Rms: -18,
    },
    'Mustang-Compare': {
        Title: 'THREE MUSTANGS, SAME PULL', Note: 'CHASE MIC. 3RD GEAR FROM 60 KM/H TO THE LIMITER: MUSTANG GT (CROSS-PLANE COYOTE, BURBLE), SHELBY GT350 (FLAT-PLANE VOODOO, SCREAM), MUSTANG GT3 (RACE COYOTE, OPEN SIDE PIPES)',
        Marks: [[0.3, 'MUSTANG GT'], [5.3, 'GT350'], [10.3, 'MUSTANG GT3']],
        Build: () => Join([DriveScene(5, 'Chase', (Time, Car) => { if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 60 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; } Car.Throttle = Time > 0.3 && Time < 4.4 ? 1 : 0; }, { Profile: MustangGt, Squeal: false }), DriveScene(5, 'Chase', (Time, Car) => { if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 60 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; } Car.Throttle = Time > 0.3 && Time < 4.4 ? 1 : 0; }, { Profile: Gt350, Squeal: false }), DriveScene(5, 'Chase', (Time, Car) => { if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 60 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; } Car.Throttle = Time > 0.3 && Time < 4.4 ? 1 : 0; }, { Profile: MustangGt3, Squeal: false })]),
        Rms: -17,
    },
    'LigierJsP320-Onboard': {
        Title: 'LIGIER JS P320 LMP3 ONBOARD', Note: 'CABIN MIC. NISSAN VK56 5.6 V8 BEHIND THE DRIVER, XTRAC 1152 SEQUENTIAL: PULL 3-4-5, STRAIGHT-CUT WHINE, HARD STOP WITH BLIPPED DOWNSHIFTS, CORNER, EXIT',
        Marks: [[0.2, 'PULL'], [7.4, 'BRAKE'], [10.2, 'CORNER'], [11.6, 'EXIT']],
        Build: () => DriveScene(14, 'Cabin', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 110 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; }
            const Pull = Time > 0.2 && Time < 7.4;
            Car.Throttle = Pull ? 1 : Time > 11.6 ? 1 : Time > 10.2 ? 0.3 : 0;
            if (Pull && Car.Rpm > 7200) Car.ShiftUp();
            Car.Brake = Time > 7.4 && Time < 10 ? 1 : 0;
            if (Time > 7.4 && Time < 10.2 && Car.Gear > 2 && Car.WheelRpm(Car.Gear - 1) < 6000) Car.ShiftDown();
            if (Time > 11.6 && Car.Rpm > 7200) Car.ShiftUp();
            Car.Steer = Time > 10 && Time < 12.6 ? 0.5 * Smoothstep(10, 10.6, Time) : 0;
        }, { Profile: LigierJsP320, Chassis: true }),
        Rms: -17,
    },
    'LigierJsP320-FlyBy': {
        Title: 'LIGIER JS P320 LMP3 FLY-BY', Note: 'TRACKSIDE MIC, 12 M FROM THE LINE: 260 KM/H IN 6TH, NISSAN V8 AT FULL THROTTLE, DOPPLER DROP',
        Marks: [[4, 'PASS']],
        Build: () => FlyBy(8, 4, LigierJsP320, 260, 6, {}),
        Rms: -18,
    },
    'GtrNismo-Onboard': {
        Title: 'GT-R NISMO ONBOARD', Note: 'CABIN MIC. PULL 3-4-5 ON BOOST, ABS STOP WITH BLIPPED DOWNSHIFTS, CORNER, SPOOL BACK UP ON EXIT',
        Marks: [[0.2, 'PULL'], [7.4, 'ABS BRAKE'], [10.2, 'CORNER'], [11.6, 'EXIT']],
        Build: () => DriveScene(14, 'Cabin', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 90 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; Car.Boost = 0.3; }
            const Pull = Time > 0.2 && Time < 7.4;
            Car.Throttle = Pull ? 1 : Time > 11.6 ? 1 : Time > 10.2 ? 0.3 : 0;
            if (Pull && Car.Rpm > 6750) Car.ShiftUp();
            Car.Brake = Time > 7.4 && Time < 10 ? 1 : 0;
            if (Time > 7.4 && Time < 10.2 && Car.Gear > 2 && Car.WheelRpm(Car.Gear - 1) < 5600) Car.ShiftDown();
            if (Time > 11.6 && Car.Rpm > 6750) Car.ShiftUp();
            Car.Steer = Time > 10 && Time < 12.6 ? 0.5 * Smoothstep(10, 10.6, Time) : 0;
        }, { Profile: GtrNismo, Chassis: true }),
        Rms: -17,
    },
    'GtrNismo-FlyBy': {
        Title: 'GT-R NISMO FLY-BY', Note: 'TRACKSIDE MIC 12 M FROM THE LINE. 5TH GEAR FLAT OUT ON BOOST AT 225 KM/H, DOPPLER, DISTANCE, GROUND BOUNCE',
        Marks: [[4, 'ABEAM']],
        Build: () => FlyBy(8, 4, GtrNismo, 225, 5, { Boost: 1 }),
        Rms: -18,
    },
    'Tyres-Screech': {
        Title: 'TYRE SCREECH', Note: 'CHASE MIC. CORNERING SQUEAL AT THE LIMIT, PANIC STOP WITH LOCK-UP, STANDING BURNOUT',
        Marks: [[0.3, 'CORNER'], [4.4, 'LOCK-UP'], [7.6, 'BURNOUT']],
        Build: () => DriveScene(12, 'Chase', (Time, Car, Bus) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 105 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; }
            if (Time < 4.4) { Car.Throttle = 0.45; Car.Steer = 0.25 + 0.5 * Smoothstep(0.3, 1.2, Time) * (0.85 + 0.15 * Math.sin(Time * 3.1)); Car.Speed = Math.max(Car.Speed, 92 / 3.6); }
            else if (Time < 7.2) { Car.Steer = 0; Car.Throttle = 0; Car.Brake = 1; if (Car.Gear > 1 && !Car.Shift) Car.ShiftDown(); }
            else
            {
                // Burnout: car held on the brakes, rear tyres spinning at full throttle in first.
                Car.Brake = 0; Car.Speed = 0; Car.Gear = 1;
                const On = Time > 7.6 && Time < 11.2;
                Car.Throttle = On ? 0.85 : 0;
                Bus.Override = { Rpm: On ? Mix(3200, 6400, Smoothstep(7.6, 8.4, Time)) + 300 * Math.sin(Time * 9) : LaFerrari.Idle + 300, Throttle: On ? 0.85 : 0.04, Load: On ? 0.7 : 0.05, Ignition: 1 };
                Bus.SquealOverride = { Speed: 0, Corner: 0, LockUp: 0, Wheelspin: On ? Smoothstep(7.6, 8.1, Time) * (0.9 + 0.1 * Math.sin(Time * 13)) : 0, Grip: 1, Surface: 'Asphalt' };
            }
        }, { EngineGain: 0.55 }),
        Rms: -17,
    },
    'Tyres-Drift': {
        Title: 'DRIFT SQUEAL ON FIVE SURFACES', Note: 'CHASE MIC, 70 KM/H SIDEWAYS: TURN-IN CHIRPS, SUSTAINED DRIFT WAIL BENDING WITH THE ANGLE, ON ASPHALT, SMOOTH CONCRETE, WET ASPHALT, COBBLES, GRAVEL (4 S EACH)',
        Marks: [[0, 'ASPHALT'], [4, 'CONCRETE'], [8, 'WET'], [12, 'COBBLES'], [16, 'GRAVEL']],
        Build: () => DriveScene(20, 'Chase', (Time, Car, Bus) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 70 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; }
            const Ground = ['Asphalt', 'Concrete', 'Wet', 'Cobbles', 'Gravel'][Math.min(4, Math.floor(Time / 4))], Local = Time % 4;
            Bus.Surface = Ground; Car.Speed = 70 / 3.6; Car.Throttle = 0.7;
            // Each 4 s: two short chirps at the grip limit, then the car is thrown sideways and held in a drift whose
            // angle swings, then it straightens.
            const Chirps = Local < 0.9 ? 0.55 * (Math.sin(Local * 11) > 0.2 ? 1 : 0.1) : 0;
            const Held = Smoothstep(0.9, 1.2, Local) * (1 - Smoothstep(3.4, 3.8, Local)) * (0.7 + 0.25 * Math.sin(Local * 2.6) + 0.05 * Math.sin(Local * 9));
            Bus.Override = { Rpm: 5200 + 900 * Held + 200 * Math.sin(Time * 5), Throttle: 0.75, Load: 0.6, Ignition: 1 };
            Bus.SquealOverride = { Speed: 70 / 3.6, Corner: Math.max(Chirps, Held), LockUp: 0, Wheelspin: 0.25 * Held, Grip: Surfaces[Ground].Grip, Surface: Ground };
        }, { EngineGain: 0.4 }),
        Rms: -18,
    },
    'Surfaces-Tour': {
        Title: 'DRIVING SURFACES', Note: 'CHASE MIC, 60 KM/H LIGHT THROTTLE. ASPHALT, WET, GRAVEL, DIRT, GRASS, COBBLES, RUMBLE STRIP, SMOOTH CONCRETE (4 S EACH)',
        Marks: Object.values(Surfaces).map((S, Index) => [Index * 4, S.Label.toUpperCase()]),
        Build: () => DriveScene(32, 'Chase', (Time, Car, Bus) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 60 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; }
            Bus.Surface = Object.keys(Surfaces)[Math.min(7, Math.floor(Time / 4))];
            Car.Throttle = 0.05 + 0.25 * Math.max(0, (60 / 3.6 - Car.Speed));          // cruise control holding 60
        }, { EngineGain: 0.3 }),
        Rms: -19,
    },
    'Chassis-Brakes': {
        Title: 'DISC BRAKES', Note: 'CHASE MIC, GT-R. 130 KM/H ABS STOP (PUMP, VALVES, PAD ROAR), LIGHT BRAKING TO A SQUEAL, CREEP GROAN TO A HALT',
        Marks: [[0.6, 'ABS STOP'], [4.6, 'LIGHT'], [8.6, 'CREEP']],
        Build: () => DriveScene(12, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 5; Car.Speed = 130 / 3.6; Car.Rpm = Car.WheelRpm(); }
            Car.Throttle = 0;
            // Scripted speeds after the ABS stop: the car rolls in neutral so the light-brake and creep phases last.
            if (Time > 0.6 && Time < 4.6) Car.Brake = Car.Speed > 28 / 3.6 ? 1 : 0.15;       // ABS stop down to ~28 km/h
            else if (Time >= 4.6 && Time < 8.6) { Car.Gear = 0; Car.Brake = 0.07; Car.Speed = Mix(26, 9, (Time - 4.6) / 4) / 3.6; }   // pads dragging: squeal
            else if (Time >= 8.6) { Car.Brake = 0.3; Car.Speed = Math.max(0, Mix(5, 0, (Time - 8.6) / 2.4)) / 3.6; }                    // creep groan to a halt
            else Car.Brake = 0;
        }, { Profile: GtrNismo, Chassis: true, EngineGain: 0.25, Squeal: true }),
        Rms: -20,
    },
    'Chassis-Suspension': {
        Title: 'SUSPENSION', Note: 'CABIN MIC, 40 KM/H. ASPHALT, KERB STRIKE, COBBLES, POTHOLE, RUTTED DIRT, RUMBLE STRIP. DAMPERS, BUMP STOPS, BUSHINGS, RATTLES',
        Marks: [[0, 'ASPHALT'], [2, 'KERB'], [3.5, 'COBBLES'], [7.5, 'POTHOLE'], [9, 'DIRT'], [13, 'RUMBLE']],
        Build: () => DriveScene(16, 'Cabin', (Time, Car, Bus) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 2; Car.Speed = 40 / 3.6; Car.Rpm = Car.WheelRpm(); Car.Automatic = false; }
            Bus.Surface = Time < 3.5 ? 'Asphalt' : Time < 7.5 ? 'Cobbles' : Time < 9 ? 'Asphalt' : Time < 13 ? 'Dirt' : 'RumbleStrip';
            if (Time >= 2 && !Bus.Kerb) { Bus.Kerb = true; Bus.Kick = 0.09; }                // kerb: 9 cm up
            if (Time >= 7.5 && !Bus.Pothole) { Bus.Pothole = true; Bus.Kick = -0.08; }       // pothole: 8 cm down
            Car.Throttle = 0.05 + 0.25 * Math.max(0, (40 / 3.6 - Car.Speed));
        }, { Profile: GtrNismo, Chassis: true, EngineGain: 0.25 }),
        Rms: -20,
    },
    'Lights-Cues': {
        Title: 'LIGHTS', Note: 'HEADLAMP SWITCH, LAMP RELAY, XENON IGNITION, LED WELCOME WITH LEVELLING MOTORS, INDICATOR RELAY',
        Marks: [[0.2, 'SWITCH'], [0.9, 'RELAY'], [1.5, 'XENON'], [4.4, 'LED'], [7.1, 'INDICATOR']],
        Build: () => CueScene(10.5, [[0.2, 'LampSwitch'], [0.9, 'HeadlampRelay'], [1.5, 'XenonIgnite'], [4.4, 'LedWelcome'], [7.1, 'Indicator', { Seconds: 3.3 }]]),
        Rms: -26,
    },
    'Cabin-Cues': {
        Title: 'CABIN', Note: 'UNLOCK, BUTTERFLY DOOR OPEN AND CLOSE, SEATBELT, BELT CHIME, START BUTTON, PADDLE, WINDOW, WIPERS, CLIMATE FAN',
        Marks: [[0.1, 'UNLOCK'], [2.4, 'DOOR'], [4.6, 'CLOSE'], [6.2, 'BELT'], [7.6, 'CHIME'], [11, 'START'], [12.9, 'PADDLE'], [13.6, 'WINDOW'], [16.8, 'WIPERS'], [20.4, 'FAN']],
        Build: () => CueScene(25, [[0.1, 'Unlock'], [2.4, 'DoorOpen'], [4.6, 'DoorClose'], [6.2, 'Seatbelt'], [7.6, 'BeltChime'], [11, 'StartButton'], [12.9, 'GearPaddle'], [13.6, 'Window'], [16.8, 'Wipers'], [20.4, 'ClimateFan', { Seconds: 4.6 }]]),
        Rms: -26,
    },
    'Impact-Explosion': { Title: 'EXPLOSION, 60 M', Note: 'BLAST WAVE, SUB THUMP, FIREBALL ROAR, DEBRIS FALLING BACK, ECHOES OFF DISTANT SURFACES', Marks: [[0.1, 'BLAST']], Build: () => CueScene(6.5, [[0, 'Explosion']]), Rms: null },
    'Impact-ExplosionFar': { Title: 'EXPLOSION, 450 M', Note: 'SAME BLAST FROM FURTHER AWAY: THE CRACK IS ABSORBED BY THE AIR, THE BOOM AND ECHOES CARRY', Marks: [[0.2, 'BLAST']], Build: () => CueScene(6.5, [[0, 'Explosion', { Distance: 450 }]]), Rms: null },
    'Impact-Shockwave': { Title: 'SHOCKWAVE', Note: 'N-WAVE DOUBLE CRACK, AIR RUSH SWEEPING PAST, GLASS AND PANELS RATTLING, ECHOES', Marks: [[0.08, 'FRONT']], Build: () => CueScene(4, [[0, 'Shockwave']]), Rms: null },
    'Impact-EmpPulse': { Title: 'EMP PULSE', Note: 'CAPACITOR CHARGE WHINE, DISCHARGE SLAP AND SUB DROP, ARCING, ELECTRONICS WINDING DOWN, EAR RING', Marks: [[0.05, 'CHARGE'], [1.78, 'DISCHARGE']], Build: () => CueScene(6, [[0, 'EmpPulse']]), Rms: null },
    'Impact-SonicBlast': { Title: 'SONIC BLAST', Note: 'SPIN-UP, BEATING PRESSURE BEAM WITH INFRASONIC BODY, CUT-OFF INTO A RUSH', Marks: [[0.62, 'BEAM'], [2.6, 'RELEASE']], Build: () => CueScene(4.5, [[0, 'SonicBlast']]), Rms: null },
    'Impact-WindowShatter': { Title: 'SIDE WINDOW SHATTER', Note: 'TEMPERED GLASS: TAP, POP, CRAZING HISS, CUBES POURING AND TINKLING', Marks: [[0.05, 'TAP'], [0.09, 'POP']], Build: () => CueScene(2.4, [[0, 'WindowShatter']]), Rms: null },
    'Impact-GlassBreak': { Title: 'GLASS BREAKING', Note: 'ANNEALED PANE: CRACK, RINGING SHARDS BREAKING AWAY, LANDING, SCATTERING, BOUNCING', Marks: [[0.05, 'CRACK']], Build: () => CueScene(2.6, [[0, 'GlassBreak']]), Rms: null },
    'Induction-SmallSingle': {
        Title: `GT-R NISMO + ${Inducers.SmallSingle.Label.toUpperCase()}`, Note: `${Inducers.SmallSingle.Note.toUpperCase()}. CHASE MIC: TWO BLIPS, PULL FROM REST, LIFT, BACK ON, LIFT`,
        Marks: [[0.5, 'BLIP'], [1.3, 'BLIP'], [2.2, 'PULL'], [7.4, 'LIFT'], [8.3, 'ON'], [9.6, 'LIFT']],
        Build: () => DriveScene(11, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            // Two blips in neutral, then a pull from rest through the gears, a sharp lift, back on, lift again.
            if (Time < 2.2) { Car.Gear = 0; Car.Throttle = (Time > 0.5 && Time < 0.8) || (Time > 1.3 && Time < 1.75) ? 1 : 0; }
            else { if (Car.Gear === 0) Car.Gear = 1; Car.Throttle = (Time < 7.4) || (Time > 8.3 && Time < 9.6) ? 1 : 0; }
        }, { Profile: GtrNismo, Induction: 'SmallSingle', Squeal: false }),
        Rms: -17,
    },
    'Induction-BigSingle': {
        Title: `GT-R NISMO + ${Inducers.BigSingle.Label.toUpperCase()}`, Note: `${Inducers.BigSingle.Note.toUpperCase()}. CHASE MIC: TWO BLIPS, PULL FROM REST, LIFT, BACK ON, LIFT`,
        Marks: [[0.5, 'BLIP'], [1.3, 'BLIP'], [2.2, 'PULL'], [7.4, 'LIFT'], [8.3, 'ON'], [9.6, 'LIFT']],
        Build: () => DriveScene(11, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            // Two blips in neutral, then a pull from rest through the gears, a sharp lift, back on, lift again.
            if (Time < 2.2) { Car.Gear = 0; Car.Throttle = (Time > 0.5 && Time < 0.8) || (Time > 1.3 && Time < 1.75) ? 1 : 0; }
            else { if (Car.Gear === 0) Car.Gear = 1; Car.Throttle = (Time < 7.4) || (Time > 8.3 && Time < 9.6) ? 1 : 0; }
        }, { Profile: GtrNismo, Induction: 'BigSingle', Squeal: false }),
        Rms: -17,
    },
    'Induction-TwinTurbo': {
        Title: `GT-R NISMO + ${Inducers.TwinTurbo.Label.toUpperCase()}`, Note: `${Inducers.TwinTurbo.Note.toUpperCase()}. CHASE MIC: TWO BLIPS, PULL FROM REST, LIFT, BACK ON, LIFT`,
        Marks: [[0.5, 'BLIP'], [1.3, 'BLIP'], [2.2, 'PULL'], [7.4, 'LIFT'], [8.3, 'ON'], [9.6, 'LIFT']],
        Build: () => DriveScene(11, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            // Two blips in neutral, then a pull from rest through the gears, a sharp lift, back on, lift again.
            if (Time < 2.2) { Car.Gear = 0; Car.Throttle = (Time > 0.5 && Time < 0.8) || (Time > 1.3 && Time < 1.75) ? 1 : 0; }
            else { if (Car.Gear === 0) Car.Gear = 1; Car.Throttle = (Time < 7.4) || (Time > 8.3 && Time < 9.6) ? 1 : 0; }
        }, { Profile: GtrNismo, Induction: 'TwinTurbo', Squeal: false }),
        Rms: -17,
    },
    'Induction-Flutter': {
        Title: `GT-R NISMO + ${Inducers.Flutter.Label.toUpperCase()}`, Note: `${Inducers.Flutter.Note.toUpperCase()}. CHASE MIC: TWO BLIPS, PULL FROM REST, LIFT, BACK ON, LIFT`,
        Marks: [[0.5, 'BLIP'], [1.3, 'BLIP'], [2.2, 'PULL'], [7.4, 'LIFT'], [8.3, 'ON'], [9.6, 'LIFT']],
        Build: () => DriveScene(11, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            // Two blips in neutral, then a pull from rest through the gears, a sharp lift, back on, lift again.
            if (Time < 2.2) { Car.Gear = 0; Car.Throttle = (Time > 0.5 && Time < 0.8) || (Time > 1.3 && Time < 1.75) ? 1 : 0; }
            else { if (Car.Gear === 0) Car.Gear = 1; Car.Throttle = (Time < 7.4) || (Time > 8.3 && Time < 9.6) ? 1 : 0; }
        }, { Profile: GtrNismo, Induction: 'Flutter', Squeal: false }),
        Rms: -17,
    },
    'Induction-Vgt': {
        Title: `GT-R NISMO + ${Inducers.Vgt.Label.toUpperCase()}`, Note: `${Inducers.Vgt.Note.toUpperCase()}. CHASE MIC: TWO BLIPS, PULL FROM REST, LIFT, BACK ON, LIFT`,
        Marks: [[0.5, 'BLIP'], [1.3, 'BLIP'], [2.2, 'PULL'], [7.4, 'LIFT'], [8.3, 'ON'], [9.6, 'LIFT']],
        Build: () => DriveScene(11, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            // Two blips in neutral, then a pull from rest through the gears, a sharp lift, back on, lift again.
            if (Time < 2.2) { Car.Gear = 0; Car.Throttle = (Time > 0.5 && Time < 0.8) || (Time > 1.3 && Time < 1.75) ? 1 : 0; }
            else { if (Car.Gear === 0) Car.Gear = 1; Car.Throttle = (Time < 7.4) || (Time > 8.3 && Time < 9.6) ? 1 : 0; }
        }, { Profile: GtrNismo, Induction: 'Vgt', Squeal: false }),
        Rms: -17,
    },
    'Induction-ElectricTurbo': {
        Title: `GT-R NISMO + ${Inducers.ElectricTurbo.Label.toUpperCase()}`, Note: `${Inducers.ElectricTurbo.Note.toUpperCase()}. CHASE MIC: TWO BLIPS, PULL FROM REST, LIFT, BACK ON, LIFT`,
        Marks: [[0.5, 'BLIP'], [1.3, 'BLIP'], [2.2, 'PULL'], [7.4, 'LIFT'], [8.3, 'ON'], [9.6, 'LIFT']],
        Build: () => DriveScene(11, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            // Two blips in neutral, then a pull from rest through the gears, a sharp lift, back on, lift again.
            if (Time < 2.2) { Car.Gear = 0; Car.Throttle = (Time > 0.5 && Time < 0.8) || (Time > 1.3 && Time < 1.75) ? 1 : 0; }
            else { if (Car.Gear === 0) Car.Gear = 1; Car.Throttle = (Time < 7.4) || (Time > 8.3 && Time < 9.6) ? 1 : 0; }
        }, { Profile: GtrNismo, Induction: 'ElectricTurbo', Squeal: false }),
        Rms: -17,
    },
    'Induction-Roots': {
        Title: `GT-R NISMO + ${Inducers.Roots.Label.toUpperCase()}`, Note: `${Inducers.Roots.Note.toUpperCase()}. CHASE MIC: TWO BLIPS, PULL FROM REST, LIFT, BACK ON, LIFT`,
        Marks: [[0.5, 'BLIP'], [1.3, 'BLIP'], [2.2, 'PULL'], [7.4, 'LIFT'], [8.3, 'ON'], [9.6, 'LIFT']],
        Build: () => DriveScene(11, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            // Two blips in neutral, then a pull from rest through the gears, a sharp lift, back on, lift again.
            if (Time < 2.2) { Car.Gear = 0; Car.Throttle = (Time > 0.5 && Time < 0.8) || (Time > 1.3 && Time < 1.75) ? 1 : 0; }
            else { if (Car.Gear === 0) Car.Gear = 1; Car.Throttle = (Time < 7.4) || (Time > 8.3 && Time < 9.6) ? 1 : 0; }
        }, { Profile: GtrNismo, Induction: 'Roots', Squeal: false }),
        Rms: -17,
    },
    'Induction-TwistedLobe': {
        Title: `GT-R NISMO + ${Inducers.TwistedLobe.Label.toUpperCase()}`, Note: `${Inducers.TwistedLobe.Note.toUpperCase()}. CHASE MIC: TWO BLIPS, PULL FROM REST, LIFT, BACK ON, LIFT`,
        Marks: [[0.5, 'BLIP'], [1.3, 'BLIP'], [2.2, 'PULL'], [7.4, 'LIFT'], [8.3, 'ON'], [9.6, 'LIFT']],
        Build: () => DriveScene(11, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            // Two blips in neutral, then a pull from rest through the gears, a sharp lift, back on, lift again.
            if (Time < 2.2) { Car.Gear = 0; Car.Throttle = (Time > 0.5 && Time < 0.8) || (Time > 1.3 && Time < 1.75) ? 1 : 0; }
            else { if (Car.Gear === 0) Car.Gear = 1; Car.Throttle = (Time < 7.4) || (Time > 8.3 && Time < 9.6) ? 1 : 0; }
        }, { Profile: GtrNismo, Induction: 'TwistedLobe', Squeal: false }),
        Rms: -17,
    },
    'Induction-TwinScrew': {
        Title: `GT-R NISMO + ${Inducers.TwinScrew.Label.toUpperCase()}`, Note: `${Inducers.TwinScrew.Note.toUpperCase()}. CHASE MIC: TWO BLIPS, PULL FROM REST, LIFT, BACK ON, LIFT`,
        Marks: [[0.5, 'BLIP'], [1.3, 'BLIP'], [2.2, 'PULL'], [7.4, 'LIFT'], [8.3, 'ON'], [9.6, 'LIFT']],
        Build: () => DriveScene(11, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            // Two blips in neutral, then a pull from rest through the gears, a sharp lift, back on, lift again.
            if (Time < 2.2) { Car.Gear = 0; Car.Throttle = (Time > 0.5 && Time < 0.8) || (Time > 1.3 && Time < 1.75) ? 1 : 0; }
            else { if (Car.Gear === 0) Car.Gear = 1; Car.Throttle = (Time < 7.4) || (Time > 8.3 && Time < 9.6) ? 1 : 0; }
        }, { Profile: GtrNismo, Induction: 'TwinScrew', Squeal: false }),
        Rms: -17,
    },
    'Induction-Centrifugal': {
        Title: `GT-R NISMO + ${Inducers.Centrifugal.Label.toUpperCase()}`, Note: `${Inducers.Centrifugal.Note.toUpperCase()}. CHASE MIC: TWO BLIPS, PULL FROM REST, LIFT, BACK ON, LIFT`,
        Marks: [[0.5, 'BLIP'], [1.3, 'BLIP'], [2.2, 'PULL'], [7.4, 'LIFT'], [8.3, 'ON'], [9.6, 'LIFT']],
        Build: () => DriveScene(11, 'Chase', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Rpm = GtrNismo.Idle; }
            // Two blips in neutral, then a pull from rest through the gears, a sharp lift, back on, lift again.
            if (Time < 2.2) { Car.Gear = 0; Car.Throttle = (Time > 0.5 && Time < 0.8) || (Time > 1.3 && Time < 1.75) ? 1 : 0; }
            else { if (Car.Gear === 0) Car.Gear = 1; Car.Throttle = (Time < 7.4) || (Time > 8.3 && Time < 9.6) ? 1 : 0; }
        }, { Profile: GtrNismo, Induction: 'Centrifugal', Squeal: false }),
        Rms: -17,
    },
    'Taycan-Launch': {
        Title: 'PORSCHE TAYCAN TURBO S LAUNCH', Note: 'CHASE MIC. READY CHIME, LAUNCH 0-200 KM/H: BOTH MOTORS WHINE, REAR 2-SPEED SHIFTS NEAR 100 KM/H, LIFT TO COAST',
        Marks: [[0.3, 'READY'], [1.5, 'LAUNCH'], [4.3, '1-2 SHIFT'], [11.5, 'LIFT']],
        Build: () => DriveScene(14, 'Chase', (Time, Car) =>
        {
            if (Time >= 0.3) Car.Start();
            if (Time > 1 && Car.Gear === 0) Car.Gear = 1;
            Car.Throttle = Time > 1.5 && Time < 11.5 ? 1 : 0;
        }, { Profile: TaycanTurboS }),
        Rms: -18,
    },
    'Taycan-Onboard': {
        Title: 'PORSCHE TAYCAN TURBO S ONBOARD', Note: 'CABIN MIC. GENTLE PULL-AWAY (INVERTER WHINE), CRUISE, FLOOR IT (STAYS IN 2ND: KICK-DOWN TO 1ST ONLY BELOW 60 KM/H), REGENERATIVE BRAKING TO A STOP',
        Marks: [[0.5, 'PULL AWAY'], [4.5, 'CRUISE'], [6.5, 'FLOOR IT'], [10.5, 'REGEN BRAKE']],
        Build: () => DriveScene(15, 'Cabin', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 1; }
            Car.Throttle = Time < 0.5 ? 0 : Time < 4.5 ? 0.35 : Time < 6.5 ? 0.12 : Time < 10.5 ? 1 : 0;
            Car.Brake = Time > 10.5 ? 0.55 : 0;
        }, { Profile: TaycanTurboS, Chassis: true }),
        Rms: -20,
    },
    'Taycan-SportSound': {
        Title: 'TAYCAN TURBO S WITH SPORT SOUND', Note: 'CABIN MIC. SAME CAR WITH A SYNTHETIC SPORT-SOUND LAYER (LIKE THE OPTIONAL SPEAKER SOUND PACKS): DEEP TONE RISING WITH SPEED',
        Marks: [[0.5, 'LAUNCH'], [3.4, 'SHIFT'], [9, 'LIFT']],
        Build: () => DriveScene(11.5, 'Cabin', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 1; }
            Car.Throttle = Time > 0.5 && Time < 9 ? 1 : 0;
        }, { Profile: TaycanTurboS, Sport: 0.05 }),
        Rms: -18,
    },
    'Taycan-FlyBy': {
        Title: 'TAYCAN TURBO S FLY-BY', Note: 'TRACKSIDE MIC 12 M FROM THE LINE. 200 KM/H IN 2ND: MOTOR WHINE, TYRES AND WIND CARRY THE PASS',
        Marks: [[4, 'ABEAM']],
        Build: () => FlyBy(8, 4, TaycanTurboS, 200, 2, { Load: 0.7 }),
        Rms: -20,
    },
    'NextGen-Launch': {
        Title: 'NEXT-GEN RACER LAUNCH (JACKSON STORM STYLE)', Note: 'CHASE MIC. ORIGINAL DESIGN: TWIN-TONE ELECTRIC WHIR LOCKED TO THE MOTOR, V8 UNDERTONE, SEQUENTIAL UPSHIFTS DROP THE PITCH',
        Marks: [[0.3, 'READY'], [1.2, 'LAUNCH'], [11.5, 'LIFT']],
        Build: () => DriveScene(14, 'Chase', (Time, Car) =>
        {
            if (Time >= 0.3) Car.Start();
            if (Time > 1 && Car.Gear === 0) Car.Gear = 1;
            Car.Throttle = Time > 1.2 && Time < 11.5 ? 1 : 0;
        }, { Profile: NextGenRacer }),
        Rms: -17,
    },
    'NextGen-Onboard': {
        Title: 'NEXT-GEN RACER ONBOARD', Note: 'CABIN MIC. PULL THROUGH 3-4-5, BRAKE WITH DOWNSHIFTS (STRAIGHT-CUT GEAR WHINE ON THE OVERRUN), CORNER, EXIT',
        Marks: [[0.2, 'PULL'], [6.5, 'BRAKE'], [9.5, 'CORNER'], [10.8, 'EXIT']],
        Build: () => DriveScene(14, 'Cabin', (Time, Car) =>
        {
            if (Time < 0.01) { Car.Running = true; Car.Gear = 3; Car.Speed = 120 / 3.6; }
            Car.Throttle = Time < 6.5 ? 1 : Time > 10.8 ? 1 : Time > 9.5 ? 0.35 : 0;
            Car.Brake = Time > 6.5 && Time < 9.3 ? 0.9 : 0;
            Car.Steer = Time > 9.3 && Time < 12 ? 0.5 * Smoothstep(9.3, 9.9, Time) : 0;
        }, { Profile: NextGenRacer, Chassis: true }),
        Rms: -17,
    },
    'NextGen-FlyBy': {
        Title: 'NEXT-GEN RACER FLY-BY', Note: 'TRACKSIDE MIC 12 M FROM THE LINE. 290 KM/H IN 6TH, FULL POWER: WHIR AND UNDERTONE WITH DOPPLER',
        Marks: [[4, 'ABEAM']],
        Build: () => FlyBy(8, 4, NextGenRacer, 290, 6, { Load: 1 }),
        Rms: -18,
    },
    'Aircraft-TrainerRunUp': { Title: 'ELECTRIC TRAINER RUN-UP', Note: 'APRON, 9 M OFF THE NOSE: 2-BLADE PROP SPINS UP TO IDLE, FULL-POWER RUN-UP, IDLE, STOP. MOTOR WHINE + INVERTER UNDER THE PROP', Marks: [[0.6, 'SPIN UP'], [2.5, 'RUN-UP'], [6.2, 'IDLE'], [8.2, 'STOP']], Build: () => CueScene(10, [[0, 'TrainerRunUp']]), Rms: null },
    'Aircraft-TrainerTakeoff': { Title: 'ELECTRIC TRAINER TAKE-OFF', Note: 'MIC 25 M BESIDE THE RUNWAY: FULL-POWER ROLL FROM 200 M, ROTATION, CLIMB-OUT PAST AND AWAY (DOPPLER, GROUND BOUNCE)', Marks: [[1.2, 'ROLL'], [12.6, 'ROTATE']], Build: () => CueScene(18, [[0, 'TrainerTakeoff']]), Rms: null },
    'Aircraft-RacerFlyover': { Title: 'ELECTRIC RACER FLY-OVER', Note: '430 KM/H, 35 M UP: 3-BLADE PROP WITH TIPS NEAR MACH 0.9 (RICH BUZZ), STRONG DOPPLER DROP', Marks: [[5, 'OVERHEAD']], Build: () => CueScene(10, [[0, 'RacerFlyover']]), Rms: null },
    'Aircraft-AirTaxi': { Title: 'EVTOL AIR TAXI', Note: '40 M FROM THE PAD: SIX 5-BLADE ROTORS SPIN UP, LIFT-OFF, HOVER (ROTOR TONES BEAT), TILT AND FLY AWAY', Marks: [[1, 'SPIN UP'], [3.5, 'LIFT-OFF'], [9, 'HOVER'], [12, 'TRANSITION']], Build: () => CueScene(18, [[0, 'AirTaxiHover']]), Rms: null },
    'Weapon-EnergyLmg': { Title: 'ENERGY LMG (DEVOTION-STYLE), FULL MAGAZINE', Note: 'SPIN-UP 5 TO 15 ROUNDS/S OVER 1.75 S, 44 ROUNDS: HEAVY RIFLE REPORT WITH BOOM AND CRACK, GEARED DRIVE AND BARREL CLATTER, ROLLING TAIL, BRASS LANDING', Marks: [[0.15, 'TRIGGER'], [1.9, 'TOP RATE']], Build: () => CueScene(5.6, [[0, 'EnergyLmg']]), Rms: null },
    'Weapon-EnergyLmgTurbo': { Title: 'ENERGY LMG + TURBOCHARGER', Note: 'SPIN-UP 6.8 TO 15 ROUNDS/S OVER 0.85 S, 44 ROUNDS: SAME REAL-GUN REPORT, TURBINE WHISTLE AND CHARGE HISS ON THE PULL, BRASS LANDING', Marks: [[0.15, 'TRIGGER'], [1.0, 'TOP RATE']], Build: () => CueScene(4.8, [[0, 'EnergyLmgTurbo']]), Rms: null },
    'Weapon-Lmg': { Title: 'LMG', Note: 'BELT-FED, 640 ROUNDS/MIN: LONG BURST AND TWO SHORT, BELT LINKS, BRASS LANDING, OUTDOOR SLAP-BACK', Marks: [[0.1, 'BURST'], [2.6, 'SHORT'], [3.5, 'SHORT']], Build: () => CueScene(5.5, [[0, 'Lmg']]), Rms: null },
    'Weapon-Smg': { Title: 'SMG', Note: '900 ROUNDS/MIN: TWO SHORT BURSTS, THEN THE REST OF A 30-ROUND MAGAZINE', Marks: [[0.1, 'BURST'], [0.75, 'BURST'], [1.5, 'DUMP']], Build: () => CueScene(4.5, [[0, 'Smg']]), Rms: null },
    'Weapon-Sniper': { Title: 'SNIPER RIFLE', Note: 'HEAVY BLAST AND THUMP, REPORT ROLLING BACK OFF THE VALLEY, BOLT CYCLE AND CASING LANDING', Marks: [[0.1, 'SHOT'], [2.6, 'BOLT']], Build: () => CueScene(5.5, [[0, 'Sniper']]), Rms: null },
    'Weapon-Turret': { Title: 'AUTOMATED TURRET', Note: 'SERVO TRAVERSE, LOCK-ON PIPS, 720 ROUNDS/MIN BURST WITH FEED MOTOR, SPIN-DOWN', Marks: [[0.1, 'TRAVERSE'], [1.15, 'LOCK'], [1.95, 'FIRE']], Build: () => CueScene(6, [[0, 'Turret']]), Rms: null },
    'Weapon-Cannon': { Title: 'TANK CANNON', Note: '120 MM: LONG BLAST WAVE, CHEST-DEEP THUMP, FIREBALL ROAR, HULL RING, ECHOES, BREECH CLANK, CASE STUB CLANG', Marks: [[0.1, 'FIRE'], [4.2, 'BREECH']], Build: () => CueScene(7, [[0, 'Cannon']]), Rms: null },
    'Weapon-AutoCannon': { Title: 'AUTOCANNON', Note: '30 MM, 210 ROUNDS/MIN: TWO BURSTS, HEAVY ACTION, BIG CASES RINGING', Marks: [[0.1, 'BURST'], [2.2, 'BURST']], Build: () => CueScene(5, [[0, 'AutoCannon']]), Rms: null },
    'Weapon-Fifty': { Title: '.50 CAL HEAVY MACHINE GUN (M2-STYLE)', Note: '550 ROUNDS/MIN IN BURSTS OF 8-10: LONG BLAST, DEEP BOOM, WIDE BULLET CRACK, RECOILING BOLT, BELT LINKS, BIG CASES ON CONCRETE', Marks: [[0.1, 'BURST'], [1.9, 'BURST'], [3.5, 'BURST']], Build: () => CueScene(6, [[0, 'Fifty']]), Rms: null },
    'Weapon-Ak47': { Title: 'AK-47 ASSAULT RIFLE', Note: '7.62X39: THREE SINGLE SHOTS, A SHORT BURST, THEN THE REST OF THE 30-ROUND MAGAZINE AT 600 ROUNDS/MIN, HEAVY BOLT CLACK, DRY CLICK', Marks: [[0.1, 'SINGLE'], [1.9, 'BURST'], [2.9, 'FULL AUTO']], Build: () => CueScene(6, [[0, 'Ak47']]), Rms: null },
    'Weapon-M4': { Title: 'M4 CARBINE', Note: '5.56 MM AT 800 ROUNDS/MIN: THREE-ROUND BURSTS, THEN LONGER, SNAPPIER AND BRIGHTER THAN THE AK, BUFFER-SPRING TWANG', Marks: [[0.1, 'BURST'], [0.8, 'BURST'], [1.5, 'BURST'], [2.5, 'LONG']], Build: () => CueScene(5, [[0, 'M4']]), Rms: null },
    'Weapon-Minigun': { Title: 'MINIGUN (M134-STYLE)', Note: 'ELECTRIC DRIVE SPINS SIX BARRELS UP IN 0.5 S TO 3000 ROUNDS/MIN: SHOTS MERGE INTO ONE TEARING ROAR, MOTOR WHINE, CASES RAINING, SPIN-DOWN', Marks: [[0.2, 'TRIGGER'], [0.7, '50 ROUNDS/S'], [2.2, 'RELEASE']], Build: () => CueScene(5.5, [[0, 'Minigun']]), Rms: null },
    'Weapon-Mg42': { Title: 'MG 42', Note: '1200 ROUNDS/MIN: TOO FAST FOR SINGLE SHOTS, THE RIPPING BUZZSAW, TWO SHORT BURSTS AND A LONG ONE', Marks: [[0.1, 'BURST'], [0.9, 'BURST'], [1.8, 'LONG']], Build: () => CueScene(5, [[0, 'Mg42']]), Rms: null },
    'Weapon-Shotgun': { Title: 'PUMP SHOTGUN, 12 GAUGE', Note: 'WIDE DEEP BOOM, NO BULLET CRACK, PUMP: FORE-END BACK, HULL EJECTED AND LANDING, FORE-END FORWARD, THREE SHOTS', Marks: [[0.1, 'SHOT'], [0.46, 'PUMP'], [1.4, 'SHOT'], [2.6, 'SHOT']], Build: () => CueScene(5, [[0, 'Shotgun']]), Rms: null },
    'Weapon-Pistol': { Title: '9 MM PISTOL', Note: 'SEMI-AUTO: SINGLE SHOTS AND DOUBLE TAPS, SHORT SHARP REPORT, SLIDE SLAM, PISTOL CASES ON CONCRETE', Marks: [[0.1, 'SHOT'], [0.8, 'DOUBLE'], [1.8, 'TRIPLE'], [3.0, 'SHOT']], Build: () => CueScene(4.5, [[0, 'Pistol']]), Rms: null },
    'Weapon-AntiMateriel': { Title: '.50 ANTI-MATERIEL RIFLE', Note: 'SEMI-AUTO .50 BMG WITH A MUZZLE BRAKE: BLAST THROWN SIDEWAYS, CHEST-DEEP BOOM, WIDE CRACK, BARREL SLAMS HOME, BIG CASES', Marks: [[0.1, 'SHOT'], [2.0, 'SHOT'], [3.4, 'SHOT']], Build: () => CueScene(6, [[0, 'AntiMateriel']]), Rms: null },
    'War-SandBlast': { Title: 'EXPLOSION IN SAND, 30 M', Note: 'DULL CRACK, BIG LOW WHUMP, THE SAND COLUMN TEARING UP, THEN SAND RAINING BACK: HISS, THOUSANDS OF GRAINS, CLODS, STONES ON A VEHICLE', Marks: [[0.08, 'BLAST'], [0.7, 'SAND FALLS']], Build: () => CueScene(7.5, [[0, 'SandBlast']]), Rms: null },
    'War-SandBlastFar': { Title: 'EXPLOSION IN SAND, 350 M', Note: 'SAME CHARGE FROM FURTHER: THE GRAINS ARE LOST, THE AIR TAKES THE CRACK, WHUMP, DULL ROAR AND DUNE ECHOES CARRY', Marks: [[0.12, 'BLAST']], Build: () => CueScene(7, [[0, 'SandBlastFar']]), Rms: null },
    'War-MortarLaunch': { Title: 'MORTAR LAUNCH', Note: 'THREE ROUNDS: DROPPED DOWN THE TUBE (SCRAPE, CLINK), FIRING PIN, HOLLOW TUBE THOONK, WHOOSH CLIMBING AWAY', Marks: [[0.62, 'FIRE'], [3.02, 'FIRE'], [5.22, 'FIRE']], Build: () => CueScene(7.5, [[0, 'MortarLaunch']]), Rms: null },
    'War-MortarIncoming': { Title: 'INCOMING MORTAR ROUND', Note: 'FIN WHISTLE AT 760 HZ DOPPLER-SHIFTED BY THE REAL PATH (55 DEG, 210 M/S, LANDS 47 M AWAY): HIGH, THEN FALLING AS IT PASSES OVER, IMPACT', Marks: [[0.3, 'WHISTLE'], [2.85, 'IMPACT']], Build: () => CueScene(7, [[0, 'MortarIncoming']]), Rms: null },
    'War-Grenade': { Title: 'FRAG GRENADE', Note: 'PIN PULLED, THROWN, LEVER FLIES OFF (4.5 S FUSE), THUD AND ROLL ON DIRT 12 M AWAY, BLAST, FRAGMENTS WHIZZING PAST, EARTH FALLING', Marks: [[0.19, 'PIN'], [1.0, 'LEVER'], [1.9, 'LANDS'], [5.54, 'BLAST']], Build: () => CueScene(9, [[0, 'Grenade']]), Rms: null },
    'War-RpgRocket': { Title: 'RPG', Note: 'BOOSTER POP AND BACK-BLAST, ROCKET MOTOR SCREAMING AWAY AT 295 M/S (DOPPLER, DARKENING), IMPACT ON A VEHICLE 300 M OFF', Marks: [[0.2, 'LAUNCH'], [0.24, 'MOTOR'], [2.1, 'IMPACT']], Build: () => CueScene(7, [[0, 'RpgRocket']]), Rms: null },
    'War-Barrage': { Title: 'ARTILLERY BARRAGE, 1-5 KM', Note: '15 HEAVY SHELLS LANDING 1.2-5 KM AWAY: DEEP BOOMS WITH THE HIGHS ABSORBED BY THE AIR, ROLLING HILL ECHOES, CONSTANT LOW RUMBLE', Marks: [[0.2, 'FIRST']], Build: () => CueScene(15, [[0, 'Barrage']]), Rms: null },
    'War-Helicopter': { Title: 'MILITARY HELICOPTER FLY-OVER', Note: '4-BLADE MAIN ROTOR 17.2 PASSES/S, TAIL ROTOR 79 HZ, TURBINES, 55 M UP, 120 KNOTS, BLADE SLAP ON THE APPROACH, DOPPLER', Marks: [[6.8, 'OVERHEAD']], Build: () => CueScene(16, [[0, 'Helicopter']]), Rms: null },
    'War-AirRaidSiren': { Title: 'AIR-RAID SIREN', Note: 'TWO-ROTOR MECHANICAL SIREN (480 / 576 HZ CHORD): MOTOR ON, WIND-UP, OFF, FALL, ON AGAIN, TURNING HEAD, 200 M AWAY IN A TOWN', Marks: [[0.3, 'ON'], [6.5, 'OFF'], [9, 'ON'], [12, 'OFF']], Build: () => CueScene(18, [[0, 'AirRaidSiren']]), Rms: null },
    'War-TankDriveBy': { Title: 'TANK PASSING', Note: '29 KM/H, 14 M AWAY: TRACK LINKS 53 CLANKS/S, SLACK TRACK SLAPS, ROAD-WHEEL SQUEAL, V12 DIESEL 1900 RPM, GROUND RUMBLE', Marks: [[7, 'PASSING']], Build: () => CueScene(14, [[0, 'TankDriveBy']]), Rms: null },
    'War-Deafened': { Title: 'SHELL SHOCK', Note: 'FIGHTING ALL ROUND, A SHELL LANDS 10 M AWAY: THEN MUFFLED (LOW-PASS FROM 180 HZ), RINGING EARS AT 4.15 KHZ, HEARTBEAT, HEARING RETURNS OVER ~10 S', Marks: [[1.5, 'BLAST'], [4, 'MUFFLED'], [11, 'BACK']], Build: () => CueScene(14, [[0, 'Deafened']]), Rms: null },
    'War-Battlefield': { Title: 'BATTLEFIELD, DISTANT FIGHTING', Note: 'AK, M4, MG 42, .50 CAL AND LMG BURSTS 120-750 M AWAY, FAR EXPLOSIONS, ROUNDS CRACKING OVERHEAD AND HITTING SAND AND ROCK, WIND', Marks: [[2, 'FIGHTING']], Build: () => CueScene(20, [[0, 'Battlefield']]), Rms: null },
    'Bullets-Supersonic': { Title: 'SUPERSONIC BULLETS PASSING', Note: 'DOWNRANGE OF A RIFLE 400 M AWAY: SHOCK-WAVE CRACK FIRST, THE MUZZLE REPORT ARRIVES 0.67 S LATER', Marks: [[0.15, 'CRACK'], [0.82, 'REPORT'], [1.3, 'CRACK'], [1.75, 'CRACK']], Build: () => CueScene(4.5, [[0, 'Supersonic']]), Rms: null },
    'Bullets-Whizz': { Title: 'SUBSONIC WHIZZ-BY', Note: 'THREE ROUNDS OR FRAGMENTS PASSING: DOPPLER PITCH DROP, VORTEX TONE, PANNING ACROSS', Marks: [[0.1, 'L TO R'], [0.85, 'R TO L'], [1.4, 'L TO R']], Build: () => CueScene(2.6, [[0, 'Whizz']]), Rms: null },
    'Bullets-Ricochet': { Title: 'RICOCHETS', Note: 'GLANCING OFF ROCK (FLIES AWAY), STEEL (ZINGS PAST THE EAR) AND CONCRETE: TUMBLING FRAGMENT, DOPPLER FROM THE FLIGHT PATH', Marks: [[0.1, 'ROCK'], [1.5, 'METAL'], [3.0, 'CONCRETE']], Build: () => CueScene(4.4, [[0, 'Ricochet']]), Rms: null },
    'Bullets-Impacts': { Title: 'BULLET IMPACTS', Note: 'SAND, DIRT, ROCK, CONCRETE, WOOD, METAL, GLASS, WATER (1.6 S APART)', Marks: Materials.map((Material, Index) => [Index * 1.6, Material.toUpperCase()]), Build: () => CueScene(14, Materials.map((Material, Index) => [Index * 1.6, `Hit${Material}`])), Rms: null },
    'Shells-Floors': { Title: 'SPENT CASES ON FOUR FLOORS', Note: '7.62 MM CASES ON CONCRETE, 5.56 MM ON A WOODEN DECK, 7.62 MM ON A STEEL FLOOR, 7.62 MM ON DIRT: RING, BOUNCE SERIES, ROLL', Marks: [[0.1, 'CONCRETE'], [4.7, 'WOOD'], [8.6, 'STEEL'], [13.3, 'DIRT']], Build: () => CueScene(16.6, [[0, 'ShellsRifle'], [4.7, 'ShellsWood'], [8.6, 'ShellsSteel'], [13.3, 'ShellsDirt']]), Rms: null },
    'Shells-Calibres': { Title: 'SPENT CASES BY CALIBRE', Note: '9 MM PISTOL, 12-GAUGE SHOTGUN, .50 BMG ON STEEL, 30 MM CANNON ON STEEL, 120 MM CASE STUB ON THE TURRET FLOOR', Marks: [[0.1, '9 MM'], [3.9, '12 GA'], [7.6, '.50'], [12.5, '30 MM'], [17.4, '120 MM']], Build: () => CueScene(20.4, [[0, 'ShellsPistol'], [3.9, 'ShellsShotgun'], [7.6, 'ShellsFifty'], [12.5, 'ShellsCannon'], [17.4, 'ShellsTank']]), Rms: null },
    'Factory-Line': { Title: 'FACTORY LINE', Note: 'HALL BED (RUMBLE, MAINS HUM, FAR MOTOR, CLANKS, AIR BLASTS, FORKLIFT BEEPS), CONVEYOR (MOTOR, 552 HZ GEAR MESH, ROLLERS, CHAIN, BOXES), STAMPING PRESS, IMPACT WRENCH', Marks: [[0.1, 'HALL'], [0.5, 'CONVEYOR'], [6.25, 'PRESS'], [10.6, 'WRENCH']], Build: () => CueScene(14, [[0, 'FactoryFloor', { Seconds: 14, Peak: 0.45 }], [0.5, 'Conveyor', { Seconds: 13.5, Peak: 0.6 }], [5, 'StampingPress', { Peak: 0.6 }], [10, 'ImpactWrench', { Peak: 0.35 }]]), Rms: -18 },
    'Factory-MetalWorks': { Title: 'METAL WORKS', Note: 'BLACKSMITH ON THE ANVIL AND QUENCH, ANGLE GRINDER CUTTING STEEL (SPIN-UP, LOAD SAG, SPARKS), MIG WELDING TWO BEADS, STEEL PIPES DROPPED', Marks: [[0.1, 'HAMMER'], [5.8, 'GRINDER'], [12.5, 'MIG WELD'], [17.7, 'PIPES']], Build: () => CueScene(22.2, [[0, 'Hammering'], [5.8, 'Grinder'], [12.5, 'Welding'], [17.7, 'PipeDrop']]), Rms: null },
    'Factory-Creaks': { Title: 'CREAKS', Note: 'STEEL BEAM UNDER LOAD (STICK-SLIP AT 26-46 HZ RINGING GIRDER MODES), HEAVY STEEL DOOR ON A DRY HINGE AND SLAM, WOODEN FLOORBOARDS, CHAIN HOIST RATCHET', Marks: [[0.4, 'BEAM'], [5.8, 'DOOR'], [10.2, 'FLOOR'], [13.6, 'HOIST']], Build: () => CueScene(18.6, [[0, 'BeamCreak'], [5.8, 'DoorCreak'], [10.2, 'FloorCreak'], [13.6, 'ChainHoist']]), Rms: null },
    'Sparks-Arcs': { Title: 'SPARKS AND ARCS', Note: 'SPARK SHOWER, SHORT CIRCUIT WITH BREAKER TRIP, HIGH-VOLTAGE CLIMBING ARC (100 HZ BUZZ), LOOSE WIRE MAKING AND BREAKING', Marks: [[0.1, 'SHOWER'], [4.4, 'SHORT'], [9, 'ARC'], [14.3, 'LOOSE WIRE']], Build: () => CueScene(18.6, [[0, 'SparkShower'], [4.2, 'ShortCircuit'], [8.9, 'ArcBuzz'], [14.1, 'LooseWire']]), Rms: null },
    'Electronics-Bench': { Title: 'ELECTRONICS', Note: 'TRANSFORMER HUM (100 HZ MAGNETOSTRICTION), CONTACTOR AND RELAY BANK, ROBOT ARM SERVOS AND GRIPPER, FLUORESCENT TUBE STARTING, COIL WHINE UNDER CHANGING LOAD', Marks: [[0.1, 'TRANSFORMER'], [6.3, 'RELAYS'], [9.9, 'SERVOS'], [15.6, 'TUBE'], [20.8, 'COIL WHINE']], Build: () => CueScene(25.2, [[0, 'TransformerHum', { Peak: 0.4 }], [6.2, 'RelayClicks'], [9.8, 'ServoArm'], [15.5, 'FluorescentFlicker', { Peak: 0.6 }], [20.7, 'CoilWhine']]), Rms: null },
    'Shells-BrassRain': { Title: 'LMG BRASS AND LINKS RAINING DOWN', Note: '32 CASES AND BELT LINKS FROM A 3 S BURST AT 640 ROUNDS/MIN LANDING ON CONCRETE, CLINKING ON THE PILE, LAST ONES ROLLING', Marks: [[0.4, 'FIRST CASE'], [3.4, 'LAST CASE']], Build: () => CueScene(5.5, [[0, 'ShellsBrassRain']]), Rms: null },
    'Impact-MetalCrash': { Title: 'METAL CRUMPLE / CRASH', Note: 'IMPACT, SHEET BUCKLING, LAMPS BURSTING, SCRAPE, LURCH, HUBCAP, SETTLING CREAKS, COOLANT HISS', Marks: [[0.08, 'IMPACT'], [0.43, 'SCRAPE'], [1.83, 'LURCH'], [2.1, 'SETTLE']], Build: () => CueScene(6, [[0, 'MetalCrash']]), Rms: null },
};

/** Trackside run: render the car dry at the source, then propagate to a fixed microphone with a moving delay. */
function FlyBy(Seconds, PassTime, Profile = LaFerrari, Kmh = 262, Gear = 6, Extra = { Motor: 0.3 })
{
    const Lead = 1.2, Count = Math.ceil((Seconds + Lead) * Rate);            // covers the ~0.9 s travel time at the start
    const SourceL = new Float32Array(Count), SourceR = new Float32Array(Count);
    const Engine = Profile.Electric ? new DriveUnit(Rate, Profile, { Pickup: 'Dry' }) : new PulseTrainEngine(Rate, Profile, { Pickup: 'Dry' });
    const Roll = new SurfaceRoll(Rate, { Pickup: 'Dry', Seed: 77 });
    const Speed = Kmh / 3.6, Wheel = Speed / Profile.TyreRadius * 60 / (2 * Math.PI);
    const Rpm = Profile.Electric ? Wheel * Profile.Motors.at(-1).Ratios[Gear - 1] : Wheel * Profile.Ratios[Gear - 1] * Profile.Final;
    const MotorRpm = (Motor, Base) => Base / Profile.Motors.at(-1).Ratios[Gear - 1] * Motor.Ratios[Math.min(Gear, Motor.Ratios.length) - 1];
    if (Profile.Electric) Engine.Units.forEach((Unit) => { Unit.Rpm.Current = MotorRpm(Unit.Motor, Rpm); Unit.Load.Current = Extra.Load ?? 0.8; });
    else Engine.Rpm.Current = Rpm;
    const Telemetry = [];
    for (let Index = 0; Index < Count; Index += Block)
    {
        const Size = Math.min(Block, Count - Index);
        const Climb = Rpm + (Index / Rate) * 55;                         // still pulling in 6th
        if (Profile.Electric) Engine.Feed({ Gear, Throttle: 1, Speed, Motors: Profile.Motors.map((Motor) => ({ Rpm: MotorRpm(Motor, Climb), Load: Extra.Load ?? 0.8 })) });
        else Engine.Feed({ Rpm: Climb, Throttle: 1, Load: 1, Gear, ...Extra });
        Engine.Render(SourceL, SourceR, Index, Size);
        Roll.Feed({ Speed });
        Roll.Render(SourceL, SourceR, Index, Size);
        Telemetry.push({ Time: Index / Rate - Lead, Rpm: Climb });
    }
    // Propagation: microphone at (0, Offset); car on the x axis at Speed · (t − PassTime). Send time solved per sample.
    const Offset = 12, Height = 1.2, Sound = 343;
    const Left = new Float32Array(Math.ceil(Seconds * Rate)), Right = new Float32Array(Left.length);
    const Air = new OnePole(Rate, 16000), AirBounce = new OnePole(Rate, 16000);
    const Read = (Buffer, Time) => { const At = (Time + Lead) * Rate, Whole = Math.floor(At), Part = At - Whole; return (Buffer[Whole] ?? 0) * (1 - Part) + (Buffer[Whole + 1] ?? 0) * Part; };
    for (let Index = 0; Index < Left.length; Index++)
    {
        const Time = Index / Rate;
        let SentAt = Time;
        for (let Guess = 0; Guess < 4; Guess++) { const X = Speed * (SentAt - PassTime); SentAt = Time - Math.hypot(X, Offset) / Sound; }
        const X = Speed * (SentAt - PassTime), Distance = Math.hypot(X, Offset);
        const Bounce = Math.hypot(X, Offset, 2 * Height);
        let SentBounce = Time - Bounce / Sound;
        // Exhaust exits at the back: louder and brighter once the car points away from the microphone.
        const Away = Smoothstep(-0.6, 0.6, X / Distance);
        const Directivity = Mix(0.62, 1.15, Away);
        Air.Set(Mix(16000, 2600, Smoothstep(15, 220, Distance)) * Mix(0.8, 1, Away));
        AirBounce.Set(Mix(9000, 1800, Smoothstep(15, 220, Bounce)));
        const Direct = Air.Run((Read(SourceL, SentAt) + Read(SourceR, SentAt)) / 2) * Offset / Distance * Directivity;
        const Ground = AirBounce.Run((Read(SourceL, SentBounce) + Read(SourceR, SentBounce)) / 2) * Offset / Bounce * Directivity * 0.55;
        const Mono = Direct + Ground;
        const [GainL, GainR] = PanGains(Math.max(-1, Math.min(1, X / 25)));
        Left[Index] = Mono * GainL * 1.4; Right[Index] = Mono * GainR * 1.4;
    }
    return { Left, Right, Telemetry: Telemetry.filter((Row) => Row.Time >= 0).map((Row) => ({ ...Row, Doppler: true })) };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

mkdirSync(AudioOut, { recursive: true }); mkdirSync(ImageOut, { recursive: true });
const Wanted = process.argv.slice(2).filter((Word) => !Word.startsWith('--')), AlsoWav = process.argv.includes('--wav');
const Rows = [];
for (const [Name, Scene] of Object.entries(Scenes))
{
    if (Wanted.length && !Wanted.includes(Name)) continue;
    const Clock = Date.now();
    const { Left, Right, Telemetry } = Scene.Build();
    Master(Left, Right, Scene.Rms);
    const Levels = WriteMp3(join(AudioOut, `${Name}.mp3`), Left, Right, Rate);
    if (AlsoWav) WriteWav(join(AudioOut, `${Name}.wav`), Left, Right, Rate);
    const Overlay = Telemetry && !Telemetry[0]?.Doppler ? Telemetry.filter((Row, Index) => Row.Rpm > 300 && Index % 3 === 0).map((Row) => [Row.Time, Row.Hz ?? Row.Rpm * Row.Firing]) : null;
    const Divisor = Overlay ? Math.round(1 / Telemetry[0].Firing) : 0;
    const Legend = Telemetry?.[0]?.Label ?? `CYAN DOTS: PREDICTED FIRING FREQUENCY (RPM / ${Divisor})`;
    DrawSpectrogram(join(ImageOut, `Audio-${Name}.png`), Left, Right, Rate, { Title: Scene.Title, Subtitle: Scene.Note + (Overlay ? `   ${Legend}` : ''), Marks: Scene.Marks, Overlay });
    const Whole = AverageSpectrum(Left, Right, Rate, 0, Left.length / Rate - 0.2, 8192);
    Rows.push({ Name, Seconds: (Left.length / Rate).toFixed(1), Peak: Levels.Peak.toFixed(1), Rms: Levels.Rms.toFixed(1), Centroid: Centroid(Whole, Rate).toFixed(0), Render: ((Date.now() - Clock) / 1000).toFixed(1), Kb: Math.round(Levels.Bytes / 1024) });
    console.log(`${Name}: ${Rows.at(-1).Seconds}s peak ${Rows.at(-1).Peak} dBFS rms ${Rows.at(-1).Rms} dBFS centroid ${Rows.at(-1).Centroid} Hz, ${Rows.at(-1).Kb} KB, rendered in ${Rows.at(-1).Render}s`);
}
writeFileSync(join(Root, '_ClaudeScratch/tmp/audio/ReelRows.json'), JSON.stringify(Rows, null, 1));
