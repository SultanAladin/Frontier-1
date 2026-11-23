// ACOUSTICS
// 🧩 Page logic for the vehicle sound bench: wakes WebAudio on the first gesture, loads VoiceRackWorklet (live car),
//    switches the car (LaFerrari / GT-R NISMO / Taycan Turbo S / Next-Gen racer), swaps turbos and superchargers, renders CabinFoley, ImpactFoley, FactoryFoley and WarFoley cues into buffers on demand, holds the Mix (road noise switch, separate wind), binds
//    keys, pedals and pads, runs the weapons' hold-to-fire trigger (shots scheduled on the audio clock at FireRate,
//    spin motor loop following the rate), and draws the tachometer from the worklet's telemetry.
// 📝 Test hooks on window.Bench: Wake(), Command(Name), Hold(Control, Amount), Cue(Name), Car(Name), Readout(),
//    RenderOffline(Seconds, Car, Induction, Gear), Fire(Weapon, Seconds) → { Fired, Seconds, Turbo }, Turbo(On), Induction(Name),
//    Sport(On), Fly(Aircraft) → seconds, Mix(Name, Level), RoadNoise(On) → on/off, Levels() → the mix as sent.
//    RenderOffline(..., Mix) takes a mix too (e.g. { Engine: 0 } to hear the road layer alone).

import { Cues as CabinCues, RenderCue as RenderCabinCue } from './CabinFoley.js';
import { Cues as ImpactCues, RenderCue as RenderImpactCue } from './ImpactFoley.js';
import { Cues as BallisticCues, RenderCue as RenderBallisticCue, Weapons, FireRate, RenderShot, RenderMotor } from './Ballistics.js';
import { Surfaces } from './ContactPatch.js';
import { Profiles as EngineProfiles } from './PulseTrainEngine.js';
import { ElectricCars } from './ElectricDrive.js';
import { Inducers } from './ForcedInduction.js';
import { Cues as AviationCues } from './ElectricAviation.js';
import { Cues as FactoryCues, RenderCue as RenderFactoryCue } from './FactoryFoley.js';
import { Cues as WarCues, RenderCue as RenderWarCue } from './WarFoley.js';

const Cues = { ...CabinCues, ...ImpactCues, ...BallisticCues, ...FactoryCues, ...WarCues };
const RenderCue = (Name, Rate, Options) => (CabinCues[Name] ? RenderCabinCue : ImpactCues[Name] ? RenderImpactCue : FactoryCues[Name] ? RenderFactoryCue : WarCues[Name] ? RenderWarCue : RenderBallisticCue)(Name, Rate, Options);
const Profiles = { ...EngineProfiles, ...ElectricCars };
const CarOrder = ['LaFerrari', 'GtrNismo', 'Demon', 'Spyder918', 'AgeraRs', 'MustangGt', 'Gt350', 'MustangGt3', 'LigierJsP320', 'TaycanTurboS', 'NextGenRacer'];
const Brands = {
    LaFerrari: { Label: 'LaFerrari', Spec: 'F140FE 6.3 L V12 · HY-KERS · 7-speed DCT · 100% synthesized', Dial: 10000, Assist: 'HY-KERS', Chip: 'KERS' },
    GtrNismo: { Label: 'GT-R NISMO', Spec: 'VR38DETT 3.8 L twin-turbo V6 · 600 PS · 6-speed DCT · AWD · 100% synthesized', Dial: 8000, Assist: 'BOOST', Chip: 'BOOST' },
    Demon: { Label: 'Challenger SRT Demon', Spec: '6.2 L supercharged HEMI V8 · 2.7 L blower, 14.5 psi · 840 hp · 8-speed auto · TransBrake (hold B) · 100% synthesized', Dial: 8000, Assist: 'BOOST', Chip: 'BOOST' },
    Spyder918: { Label: '918 Spyder', Spec: '4.6 L flat-plane V8, 9150 rpm · 2 e-motors · 887 hp · 7-speed PDK · top pipes · 100% synthesized', Dial: 10000, Assist: 'E-MOTORS', Chip: 'E-BOOST' },
    AgeraRs: { Label: 'Agera RS', Spec: '5.0 L twin-turbo cross-plane V8 · 1160 hp · 7-speed paddle shift · RWD · 100% synthesized', Dial: 8000, Assist: 'BOOST', Chip: 'BOOST' },
    MustangGt: { Label: 'Mustang GT', Spec: '5.0 L Coyote cross-plane V8 · 486 hp · 7500 rpm · 6-speed manual · 100% synthesized', Dial: 8000, Assist: 'NO ASSIST · I ADDS BOOST', Chip: 'BOOST' },
    Gt350: { Label: 'Shelby GT350', Spec: '5.2 L Voodoo flat-plane V8 · 526 hp · 8250 rpm · Tremec 6-speed · 100% synthesized', Dial: 9000, Assist: 'NO ASSIST · I ADDS BOOST', Chip: 'BOOST' },
    MustangGt3: { Label: 'Mustang GT3', Spec: 'LMGT3 race car · 5.4 L race Coyote V8 · ~550 hp · Xtrac 6-speed sequential · side pipes · 100% synthesized', Dial: 8000, Assist: 'NO ASSIST · I ADDS BOOST', Chip: 'BOOST' },
    LigierJsP320: { Label: 'Ligier JS P320', Spec: 'LMP3 prototype · Nissan VK56 5.6 L V8 · ~460 hp · Xtrac 6-speed sequential · 950 kg · 100% synthesized', Dial: 8000, Assist: 'NO ASSIST · I ADDS BOOST', Chip: 'BOOST' },
    TaycanTurboS: { Label: 'Taycan Turbo S', Spec: 'Two PSM motors · 800 V · 2-speed rear (15.5 / 8.05) · AWD · 100% synthesized', Dial: 16000, Assist: 'DRIVE / REGEN', Chip: 'REGEN', Electric: true },
    NextGenRacer: { Label: 'Next-Gen racer', Spec: 'Jackson Storm-style original · 634 kW motor · 6-speed sequential · V8 undertone · 100% synthesized', Dial: 18000, Assist: 'DRIVE / REGEN', Chip: 'REGEN', Electric: true },
};
let Induction = null, SportSound = false;
let CarName = 'LaFerrari';

const Find = (Id) => document.getElementById(Id);
const Reel = [
    ['LaFerrari-ColdStart', 'LaFerrari cold start'], ['LaFerrari-Launch', 'LaFerrari launch'], ['LaFerrari-Onboard', 'LaFerrari onboard'],
    ['LaFerrari-Limiter', 'LaFerrari limiter'], ['LaFerrari-FlyBy', 'LaFerrari fly-by'], ['Tyres-Screech', 'Tyre screech'], ['Tyres-Drift', 'Drift squeal on five surfaces'],
    ['GtrNismo-ColdStart', 'GT-R cold start'], ['GtrNismo-Launch', 'GT-R launch'], ['GtrNismo-Onboard', 'GT-R onboard'],
    ['GtrNismo-FlyBy', 'GT-R fly-by'], ['Demon-ColdStart', 'Demon cold start'], ['Demon-Launch', 'Demon TransBrake launch'], ['Demon-RollOn', 'Demon roll-on (no lag)'], ['TurboLag-Compare', 'Turbo lag compared'], ['Spyder918-Launch', '918 Spyder launch'], ['Spyder918-FlyBy', '918 Spyder fly-by'], ['AgeraRs-Launch', 'Agera RS launch'], ['AgeraRs-FlyBy', 'Agera RS fly-by'], ['Road-NoiseSwitch', 'Road noise: full, default, off'], ['MustangGt-StartLaunch', 'Mustang GT start and launch'], ['Gt350-Launch', 'Shelby GT350 launch'], ['MustangGt3-Onboard', 'Mustang GT3 onboard'], ['MustangGt3-FlyBy', 'Mustang GT3 fly-by'], ['Mustang-Compare', 'Three Mustangs, same pull'], ['LigierJsP320-Onboard', 'Ligier JS P320 onboard'], ['LigierJsP320-FlyBy', 'Ligier JS P320 fly-by'], ['Surfaces-Tour', 'Surfaces tour'], ['Chassis-Brakes', 'Disc brakes'],
    ['Chassis-Suspension', 'Suspension'], ['Lights-Cues', 'Lights'], ['Cabin-Cues', 'Cabin'],
    ['Impact-Explosion', 'Explosion 60 m'], ['Impact-ExplosionFar', 'Explosion 450 m'], ['Impact-Shockwave', 'Shockwave'],
    ['Impact-EmpPulse', 'EMP pulse'], ['Impact-SonicBlast', 'Sonic blast'], ['Impact-WindowShatter', 'Window shatter'],
    ['Impact-GlassBreak', 'Glass breaking'], ['Impact-MetalCrash', 'Metal crash'],
    ['Weapon-EnergyLmg', 'Energy LMG'], ['Weapon-EnergyLmgTurbo', 'Energy LMG + Turbo'], ['Weapon-Lmg', 'LMG'], ['Weapon-Smg', 'SMG'],
    ['Weapon-Sniper', 'Sniper rifle'], ['Weapon-Turret', 'Turret'], ['Weapon-Cannon', 'Tank cannon'], ['Weapon-AutoCannon', 'Autocannon'],
    ['Weapon-Fifty', '.50 cal HMG'], ['Weapon-Ak47', 'AK-47'], ['Weapon-M4', 'M4 carbine'], ['Weapon-Minigun', 'Minigun'], ['Weapon-Mg42', 'MG 42'],
    ['Weapon-Shotgun', 'Pump shotgun'], ['Weapon-Pistol', '9 mm pistol'], ['Weapon-AntiMateriel', '.50 anti-materiel rifle'],
    ['War-SandBlast', 'Explosion in sand 30 m'], ['War-SandBlastFar', 'Explosion in sand 350 m'], ['War-MortarLaunch', 'Mortar launch'], ['War-MortarIncoming', 'Incoming mortar'],
    ['War-Grenade', 'Frag grenade'], ['War-RpgRocket', 'RPG'], ['War-Barrage', 'Artillery barrage'], ['War-Helicopter', 'Helicopter fly-over'],
    ['War-AirRaidSiren', 'Air-raid siren'], ['War-TankDriveBy', 'Tank passing'], ['War-Deafened', 'Shell shock'], ['War-Battlefield', 'Battlefield'],
    ['Bullets-Supersonic', 'Supersonic crack'], ['Bullets-Whizz', 'Whizz-by'], ['Bullets-Ricochet', 'Ricochets'], ['Bullets-Impacts', 'Bullet impacts'],
    ['Shells-Floors', 'Cases on four floors'], ['Shells-Calibres', 'Cases by calibre'], ['Shells-BrassRain', 'LMG brass rain'],
    ['Factory-Line', 'Factory line'], ['Factory-MetalWorks', 'Metal works'], ['Factory-Creaks', 'Creaks'], ['Sparks-Arcs', 'Sparks and arcs'], ['Electronics-Bench', 'Electronics'],
    ...Object.entries(Inducers).map(([Name, Model]) => [`Induction-${Name}`, `GT-R + ${Model.Label.toLowerCase()}`]),
    ['Taycan-Launch', 'Taycan launch'], ['Taycan-Onboard', 'Taycan onboard'], ['Taycan-SportSound', 'Taycan sport sound'], ['Taycan-FlyBy', 'Taycan fly-by'],
    ['NextGen-Launch', 'Next-Gen launch'], ['NextGen-Onboard', 'Next-Gen onboard'], ['NextGen-FlyBy', 'Next-Gen fly-by'],
    ['Aircraft-TrainerRunUp', 'Trainer run-up'], ['Aircraft-TrainerTakeoff', 'Trainer take-off'], ['Aircraft-RacerFlyover', 'Racer fly-over'], ['Aircraft-AirTaxi', 'eVTOL air taxi'],
];
const AircraftClips = { TrainerRunUp: 'Aircraft-TrainerRunUp', TrainerTakeoff: 'Aircraft-TrainerTakeoff', RacerFlyover: 'Aircraft-RacerFlyover', AirTaxiHover: 'Aircraft-AirTaxi' };
const SurfaceLooks = {
    Asphalt: 'radial-gradient(circle at 30% 30%, #4a4b50 0 1px, transparent 1.5px) 0 0/7px 7px, linear-gradient(160deg, #2c2d31, #1a1b1e)',
    Wet: 'linear-gradient(100deg, transparent 40%, rgba(160,200,255,0.25) 50%, transparent 60%), linear-gradient(160deg, #1d2430, #0f1218)',
    Gravel: 'radial-gradient(circle at 40% 40%, #a39a8a 0 2px, transparent 3px) 0 0/9px 9px, radial-gradient(circle, #6f685e 0 2px, transparent 3px) 4px 5px/11px 11px, #4a453e',
    Dirt: 'radial-gradient(circle at 60% 40%, #7a5636 0 3px, transparent 5px) 0 0/16px 14px, linear-gradient(160deg, #5c3f27, #3a2717)',
    Grass: 'repeating-linear-gradient(80deg, #2f6d2c 0 2px, #3f8a37 2px 4px, #24541f 4px 6px)',
    Cobbles: 'radial-gradient(ellipse at center, #6b6a66 0 55%, #2a2927 60%) 0 0/18px 14px, #2a2927',
    RumbleStrip: 'repeating-linear-gradient(90deg, #d8262a 0 14px, #f2f2f2 14px 28px)',
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    AUDIO
//------------------------------------------------------------------------------------------------------------------------

const Audio = { Context: null, Rack: null, Master: null, Foley: null, Buffers: new Map(), Loops: new Map(), Waking: null };
let Readout = { Rpm: 0, Kmh: 0, Gear: 0, Running: false, Starting: false, Motor: 0 };

/** Builds the graph once: worklet → engine bus, foley bus → master gain → limiter → speakers. */
async function Wake()
{
    if (Audio.Context) { if (Audio.Context.state !== 'running') await Audio.Context.resume(); return; }
    if (Audio.Waking) return Audio.Waking;
    Audio.Waking = (async () =>
    {
        const Context = new AudioContext({ latencyHint: 'interactive' });
        await Context.audioWorklet.addModule(new URL('./VoiceRackWorklet.js?v=22', import.meta.url));
        const Limiter = Context.createDynamicsCompressor();
        Limiter.threshold.value = -6; Limiter.knee.value = 4; Limiter.ratio.value = 12; Limiter.attack.value = 0.002; Limiter.release.value = 0.12;
        const Master = Context.createGain(); Master.gain.value = 0.8;
        const Foley = Context.createGain(); Foley.gain.value = 1;
        const Rack = new AudioWorkletNode(Context, 'voice-rack', { numberOfInputs: 0, outputChannelCount: [2], processorOptions: { Car: CarName, Induction } });
        Rack.port.onmessage = (Message) => { if (Message.data.Fault) { console.error('voice-rack fault', Message.data.Fault); return; } Readout = Message.data; };
        Rack.connect(Master); Foley.connect(Master); Master.connect(Limiter); Limiter.connect(Context.destination);
        Object.assign(Audio, { Context, Rack, Master, Foley });
        await Context.resume();
        Find('Wake').classList.add('Live'); Find('Wake').querySelector('span').textContent = `Sound live · ${Math.round(Context.sampleRate / 100) / 10} kHz synthesis`;
        SendControls(); SendMix();
    })();
    return Audio.Waking;
}

const Controls = { Throttle: 0, Brake: 0, Steer: 0, Handbrake: false, Automatic: true, Traction: true, Surface: 'Asphalt', Pickup: 'Chase', Turbocharger: false, TransBrake: false, LagScale: 1 };
const SendControls = () => { const { Turbocharger, ...Car } = Controls; Audio.Rack?.port.postMessage({ Controls: Car }); };
// Mix sliders. Road (tyre roar on the surface) defaults to 40 % and has an on/off switch (M); Wind is its own slider (80 %).
const Levels = { Master: 0.8, Engine: 1, Road: 0.4, Wind: 0.8, Tyres: 1, Chassis: 1, Foley: 1 };   // Foley also carries weapons and bullets
let RoadOn = true;
const SentMix = () => ({ Engine: Levels.Engine, Road: RoadOn ? Levels.Road : 0, Wind: Levels.Wind, Tyres: Levels.Tyres, Chassis: Levels.Chassis });
const SendMix = () => Audio.Rack?.port.postMessage({ Mix: SentMix() });
const Command = async (Name) => { await Wake(); Audio.Rack.port.postMessage({ Command: Name }); };

/** Plays a cue (rendered once per sample rate, then cached). Loop cues toggle on and off. */
async function Cue(Name)
{
    await Wake();
    const Context = Audio.Context, Pad = document.querySelector(`[data-cue="${Name}"]`), Spec = Cues[Name];
    if (Spec.Loop && Audio.Loops.has(Name)) { Audio.Loops.get(Name).stop(); Audio.Loops.delete(Name); Pad?.classList.remove('Looping'); return; }
    if (!Audio.Buffers.has(Name))
    {
        const Seconds = Name === 'Indicator' ? 60 / 85 * 4 : Name === 'ClimateFan' ? 8 : undefined;
        const { Left, Right } = RenderCue(Name, Context.sampleRate, Seconds ? { Seconds } : {});
        const Buffer = Context.createBuffer(2, Left.length, Context.sampleRate);
        Buffer.copyToChannel(Left, 0); Buffer.copyToChannel(Right, 1);
        Audio.Buffers.set(Name, Buffer);
    }
    const Source = Context.createBufferSource();
    Source.buffer = Audio.Buffers.get(Name);
    if (Spec.Loop)
    {
        Source.loop = true;
        if (Name === 'ClimateFan') { Source.loopStart = 2; Source.loopEnd = Source.buffer.duration - 0.06; }
        Audio.Loops.set(Name, Source); Pad?.classList.add('Looping');
    }
    else { Pad?.classList.add('Ring'); Source.onended = () => Pad?.classList.remove('Ring'); }
    Source.connect(Audio.Foley); Source.start();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRIGGER
//------------------------------------------------------------------------------------------------------------------------

const Trigger = { Name: null, Timer: 0, Start: 0, Next: 0, Fired: 0, Turbo: false, Motor: null, Level: null, Top: 1, Spin: 0, Last: null };
const ShotBanks = new Map(), MotorLoops = new Map();
let Picked = 'EnergyLmg';

const ToBuffer = ({ Left, Right }) =>
{
    const Buffer = Audio.Context.createBuffer(2, Left.length, Audio.Context.sampleRate);
    Buffer.copyToChannel(Left, 0); Buffer.copyToChannel(Right, 1);
    return Buffer;
};

/** Six shot variants per weapon (rendered once), so a long burst never repeats one sample. */
const ShotBank = (Name) => ShotBanks.get(Name) ?? ShotBanks.set(Name, Array.from({ length: 6 }, (_, Index) => ToBuffer(RenderShot(Name, Audio.Context.sampleRate, Index + 1)))).get(Name);

/** Pulls the trigger: shots are scheduled ~120 ms ahead on the audio clock, each spaced by 1 / FireRate(held time). */
async function PullTrigger(Name)
{
    await Wake();
    ReleaseTrigger();
    const Context = Audio.Context, Weapon = Weapons[Name], Turbo = !!(Controls.Turbocharger && Weapon.Turbo), Shots = ShotBank(Name);
    Picked = Name; MarkPicked();
    Object.assign(Trigger, { Name, Turbo, Start: Context.currentTime + 0.03, Fired: 0 });
    Trigger.Next = Trigger.Start;
    if (Weapon.Motor)
    {
        const Key = `${Name}${Turbo ? '+' : ''}`;
        if (!MotorLoops.has(Key)) { const Motor = RenderMotor(Name, Context.sampleRate, Turbo); MotorLoops.set(Key, { Buffer: ToBuffer(Motor), Top: Motor.Top }); }
        const { Buffer, Top } = MotorLoops.get(Key);
        const Source = Context.createBufferSource(), Level = Context.createGain();
        Source.buffer = Buffer; Source.loop = true;
        Trigger.Top = Top; Trigger.Spin = FireRate(Name, 0, Turbo) / Top;
        Source.playbackRate.setValueAtTime(Trigger.Spin * 0.4, Context.currentTime);
        Source.playbackRate.linearRampToValueAtTime(Trigger.Spin, Trigger.Start);
        Level.gain.setValueAtTime(0.0001, Context.currentTime); Level.gain.exponentialRampToValueAtTime(1, Trigger.Start + 0.02);
        Source.connect(Level); Level.connect(Audio.Foley); Source.start();
        Object.assign(Trigger, { Motor: Source, Level });
    }
    const Schedule = () =>
    {
        const Now = Context.currentTime;
        while (Trigger.Name === Name && Trigger.Next < Now + 0.12 && Trigger.Fired < Weapon.Magazine)
        {
            const Source = Context.createBufferSource();
            Source.buffer = Shots[(Trigger.Fired * 5 + (Math.random() * 3 | 0)) % Shots.length];
            Source.connect(Audio.Foley); Source.start(Trigger.Next);
            const Held = Trigger.Next - Trigger.Start, Rate = FireRate(Name, Held, Turbo);
            if (Trigger.Motor) { Trigger.Spin = Rate / Trigger.Top; Trigger.Motor.playbackRate.linearRampToValueAtTime(Trigger.Spin, Trigger.Next); }
            Trigger.Fired++; Trigger.Next += 1 / Rate;
        }
        if (Trigger.Name === Name && Trigger.Fired >= Weapon.Magazine) ReleaseTrigger();      // magazine empty
    };
    Schedule();
    Trigger.Timer = setInterval(Schedule, 25);
    document.querySelector(`[data-weapon="${Name}"]`)?.classList.add('Firing');
}

/** Lets go: no more shots; the spin motor winds down over about a second. */
function ReleaseTrigger()
{
    if (!Trigger.Name) return;
    clearInterval(Trigger.Timer);
    const Context = Audio.Context, Now = Context.currentTime;
    Trigger.Last = { Weapon: Trigger.Name, Fired: Trigger.Fired, Seconds: +(Trigger.Next - Trigger.Start).toFixed(3), Turbo: Trigger.Turbo };
    if (Trigger.Motor)
    {
        const { Motor, Level } = Trigger, From = Math.max(Now, Trigger.Next - 0.02);
        Motor.playbackRate.cancelScheduledValues(From); Motor.playbackRate.setValueAtTime(Trigger.Spin, From);
        Motor.playbackRate.exponentialRampToValueAtTime(Math.max(0.05, Trigger.Spin * 0.08), From + 1.0);
        Level.gain.cancelScheduledValues(From); Level.gain.setValueAtTime(1, From); Level.gain.setTargetAtTime(0.0001, From, 0.3);
        Motor.stop(From + 1.4);
    }
    document.querySelector(`[data-weapon="${Trigger.Name}"]`)?.classList.remove('Firing');
    Object.assign(Trigger, { Name: null, Motor: null, Level: null });
}

function MarkPicked() { for (const Pad of document.querySelectorAll('[data-weapon]')) Pad.classList.toggle('Picked', Pad.dataset.weapon === Picked); }

function ToggleTurbo(On = !Controls.Turbocharger)
{
    Controls.Turbocharger = On;
    const Button = Find('Turbocharger');
    Button?.classList.toggle('On', On);
    if (Button) Button.querySelector('small').textContent = On ? 'Hop-up fitted · 6.8 to 15 rounds/s in 0.85 s' : 'Off · 5 to 15 rounds/s in 1.75 s';
}

/** Test hook: hold the trigger for Seconds, release, report how many rounds went out. */
async function Fire(Name, Seconds = 1)
{
    await PullTrigger(Name);
    await new Promise((Done) => setTimeout(Done, Seconds * 1000));
    if (Trigger.Name === Name) ReleaseTrigger();
    return Trigger.Last;
}

/**
 * Proof path for headless checks: renders Seconds of the live worklet in an OfflineAudioContext with the start button
 * pressed and the throttle floored in neutral (it should end bouncing on the limiter), and returns peak / RMS levels,
 * the count of non-finite samples and the final telemetry.
 */
async function RenderOffline(Seconds = 5, Car = CarName, Inducer = null, Gear = 0, Mix = null)
{
    const Rate = 48000, Context = new OfflineAudioContext(2, Math.round(Seconds * Rate), Rate);
    await Context.audioWorklet.addModule(new URL('./VoiceRackWorklet.js?v=22', import.meta.url));
    const Rack = new AudioWorkletNode(Context, 'voice-rack', { numberOfInputs: 0, outputChannelCount: [2], processorOptions: { Start: true, Throttle: 1, Car, Induction: Inducer, Gear, Mix } });
    let Last = null; Rack.port.onmessage = (Message) => { Last = Message.data; };
    Rack.connect(Context.destination);
    const Rendered = await Context.startRendering();
    const Left = Rendered.getChannelData(0);
    let Peak = 0, Sum = 0, Bad = 0;
    for (const Sample of Left) { if (!Number.isFinite(Sample)) Bad++; Peak = Math.max(Peak, Math.abs(Sample)); Sum += Sample * Sample; }
    return { Seconds, Peak: +(20 * Math.log10(Peak)).toFixed(1), Rms: +(10 * Math.log10(Sum / Left.length)).toFixed(1), NonFinite: Bad, Telemetry: Last };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    UI
//------------------------------------------------------------------------------------------------------------------------

function BuildPanels()
{
    Find('Surfaces').append(...Object.entries(Surfaces).map(([Name, Spec], Index) =>
    {
        const Chip = document.createElement('button');
        Chip.className = 'Surface' + (Name === Controls.Surface ? ' On' : '');
        Chip.dataset.surface = Name;
        Chip.style.background = SurfaceLooks[Name];
        Chip.innerHTML = `<small>${Index + 1}</small><b>${Spec.Label}</b>`;
        Chip.onclick = () => PickSurface(Name);
        return Chip;
    }));
    for (const [Name, Spec] of Object.entries(Cues))
    {
        if (Spec.Group === 'Weapons') continue;                         // weapons get trigger pads below
        const Pad = document.createElement('button');
        Pad.className = 'Pad'; Pad.dataset.cue = Name;
        Pad.innerHTML = `<b>${Spec.Label}</b><small>${Spec.Loop ? 'Loop · tap to toggle' : `${Spec.Seconds.toFixed(1)} s`}</small>`;
        Pad.onclick = () => Cue(Name);
        const Shelf = { Lights: 'PadsLights', Cabin: 'PadsCabin', Bullets: 'PadsBullets', Impacts: 'PadsBullets', Shells: 'PadsShells', Factory: 'PadsFactory', Creaks: 'PadsFactory', Sparks: 'PadsSparks', War: 'PadsWar' }[Spec.Group] ?? 'PadsImpacts';
        Find(Shelf).append(Pad);
    }
    for (const [Name, Spec] of Object.entries(Weapons))
    {
        const Pad = document.createElement('button');
        Pad.className = 'Pad Trigger'; Pad.dataset.weapon = Name;
        Pad.innerHTML = `<b>${Spec.Label}</b><small>${Spec.Note} · hold</small>`;
        const Let = () => { if (Trigger.Name === Name) ReleaseTrigger(); };
        Pad.addEventListener('pointerdown', (Event) => { Pad.setPointerCapture(Event.pointerId); PullTrigger(Name); });
        Pad.addEventListener('pointerup', Let); Pad.addEventListener('pointercancel', Let);
        Find('PadsWeapons').append(Pad);
    }
    for (const Name of ['Sniper', 'Cannon'])
    {
        const Pad = document.createElement('button');
        Pad.className = 'Pad'; Pad.dataset.cue = Name;
        Pad.innerHTML = `<b>${Cues[Name].Label}</b><small>tap · ${Cues[Name].Seconds.toFixed(1)} s</small>`;
        Pad.onclick = () => Cue(Name);
        Find('PadsWeapons').append(Pad);
    }
    const Turbo = document.createElement('button');
    Turbo.className = 'Pad Hop'; Turbo.id = 'Turbocharger';
    Turbo.innerHTML = '<b>Turbocharger</b><small></small>';
    Turbo.onclick = () => ToggleTurbo();
    Find('PadsWeapons').append(Turbo);
    ToggleTurbo(false); MarkPicked();
    const Stock = document.createElement('button');
    Stock.className = 'Pad'; Stock.dataset.induction = '';
    Stock.innerHTML = '<b>Stock</b><small>the car as built</small>';
    Stock.onclick = () => PickInduction(null);
    Find('PadsInduction').append(Stock);
    for (const [Name, Model] of Object.entries(Inducers))
    {
        const Pad = document.createElement('button');
        Pad.className = 'Pad'; Pad.dataset.induction = Name; Pad.title = Model.Note;
        Pad.innerHTML = `<b>${Model.Label}</b><small>${Model.Family === 'Turbo' ? 'turbo' : 'supercharger'} · ${Model.Note.split(':')[0].toLowerCase()}</small>`;
        Pad.onclick = () => PickInduction(Name);
        Find('PadsInduction').append(Pad);
    }
    for (const Name of ['TaycanTurboS', 'NextGenRacer'])
    {
        const Pad = document.createElement('button');
        Pad.className = 'Pad'; Pad.dataset.car = Name;
        Pad.innerHTML = `<b>${Brands[Name].Label}</b><small>${Name === 'TaycanTurboS' ? 'drive it · two motor whines, 2-speed shift' : 'drive it · Storm-style whir, sequential box'}</small>`;
        Pad.onclick = () => PickCar(Name);
        Find('PadsElectric').append(Pad);
    }
    const Sport = document.createElement('button');
    Sport.className = 'Pad Hop'; Sport.id = 'SportSound';
    Sport.innerHTML = '<b>Sport sound</b><small>synthetic speaker layer for the EVs</small>';
    Sport.onclick = () => ToggleSport();
    Find('PadsElectric').append(Sport);
    for (const [Name, Spec] of Object.entries(AviationCues))
    {
        const Pad = document.createElement('button');
        Pad.className = 'Pad'; Pad.dataset.fly = Name;
        Pad.innerHTML = `<b>${Spec.Label}</b><small>aircraft · ${Spec.Seconds} s</small>`;
        Pad.onclick = () => Fly(Name);
        Find('PadsElectric').append(Pad);
    }
    MarkPower();
    for (const [Name, Label, Note] of [['Kerb', 'Kerb strike', 'K · 9 cm up'], ['Pothole', 'Pothole', 'P · 8 cm down']])
    {
        const Pad = document.createElement('button');
        Pad.className = 'Pad'; Pad.dataset.kick = Name;
        Pad.innerHTML = `<b>${Label}</b><small>${Note} · needs speed</small>`;
        Pad.onclick = () => Kick(Name);
        Find('PadsChassis').append(Pad);
    }
    const Hint = document.createElement('p');
    Hint.className = 'Hint';
    Hint.textContent = 'Brakes: hold S at speed for ABS (pump buzz, valve clicks), brush it gently below 40 km/h for squeal, creep to a stop for groan. Suspension: cobbles, dirt and the rumble strip drive the dampers; the cabin mic hears it best.';
    Find('PadsChassis').append(Hint);
    const Switch = document.createElement('button');
    Switch.className = 'Toggle RoadSwitch'; Switch.id = 'RoadNoise'; Switch.dataset.on = 'On';
    Switch.innerHTML = '<small>Road noise · M</small><b>On</b>';
    Switch.onclick = () => SetRoadNoise(!RoadOn);
    Find('Mix').append(Switch);
    const Shown = { Road: 'Road roar', Wind: 'Wind' };
    for (const [Name, Start] of Object.entries(Levels))
    {
        const Row = document.createElement('label');
        Row.dataset.mix = Name;
        Row.innerHTML = `<span>${Shown[Name] ?? Name}</span><input type="range" min="0" max="1.5" step="0.01" value="${Start}"><output>${Math.round(Start * 100)}</output>`;
        const Slider = Row.querySelector('input');
        Slider.oninput = () => SetLevel(Name, +Slider.value);
        Find('Mix').append(Row);
    }
    const Note = document.createElement('p');
    Note.className = 'Hint';
    Note.textContent = 'The roar at speed is the tyres on the road, not the wind (wind sits 13–30 dB under it). Road starts at 40 % and eases off above 150 km/h; M switches it off entirely.';
    Find('Mix').append(Note);
    Find('Reel').append(...Reel.map(([File, Label]) =>
    {
        const Row = document.createElement('div');
        Row.innerHTML = `<span>${Label}</span><audio controls preload="none" src="../../../Exhibits/CabinHmi/Audio/${File}.mp3"></audio><a href="../../../Exhibits/CabinHmi/Images/Audio-${File}.png" target="_blank">plate</a>`;
        return Row;
    }));
}

/** Sets one Mix level and moves its slider: Master and Foley are page gains, the rest go to the worklet. */
function SetLevel(Name, Level)
{
    Levels[Name] = Level;
    const Row = document.querySelector(`[data-mix="${Name}"]`);
    if (Row) { Row.querySelector('input').value = Level; Row.querySelector('output').textContent = Math.round(Level * 100); }
    if (Name === 'Master') Audio.Master && (Audio.Master.gain.value = Level);
    else if (Name === 'Foley') Audio.Foley && (Audio.Foley.gain.value = Level);
    else SendMix();
}

/** Road noise on/off (tyre roar, hiss, tread, stones, water, knocks); the Road slider keeps its level for when it returns. */
function SetRoadNoise(On)
{
    RoadOn = !!On;
    const Switch = Find('RoadNoise');
    if (Switch) { Switch.dataset.on = RoadOn ? 'On' : 'Off'; Switch.querySelector('b').textContent = RoadOn ? 'On' : 'Off'; }
    document.querySelector('[data-mix="Road"]')?.classList.toggle('Muted', !RoadOn);
    SendMix();
    return RoadOn;
}

/** Kerb or pothole: a one-off bump under the tyre (the worklet runs it through the quarter-car). */
async function Kick(Name)
{
    await Command(Name);
    const Pad = document.querySelector(`[data-kick="${Name}"]`);
    Pad?.classList.add('Ring'); setTimeout(() => Pad?.classList.remove('Ring'), 400);
}

/** Switches the car: the worklet builds the new engine and integrator (switched off); the page relabels and redraws. */
async function PickCar(Name)
{
    if (!Profiles[Name]) return;
    CarName = Name;
    const Brand = Brands[Name], Button = Find('CarPick');
    Button.dataset.on = Name; Button.querySelector('b').textContent = Brand.Label;
    Find('BrandName').textContent = Brand.Label; Find('BrandSpec').textContent = Brand.Spec;
    Find('KersLabel').textContent = Brand.Assist; Find('ChipKers').textContent = Brand.Chip;
    Find('Start').querySelector('span').textContent = Brand.Electric ? 'POWER' : 'ENGINE';
    document.body.dataset.car = Name;
    MarkPower();
    await Wake();
    Audio.Rack.port.postMessage({ Car: Name });
    SendControls();
}

/** Swaps the turbo or supercharger (null = stock). Combustion cars only; the car keeps running through the swap. */
async function PickInduction(Name)
{
    Induction = Inducers[Name] ? Name : null;
    if (Brands[CarName].Electric) await PickCar('GtrNismo');
    MarkPower();
    await Wake();
    Audio.Rack.port.postMessage({ Induction: Induction ?? '' });
}

function ToggleSport(On = !SportSound)
{
    SportSound = On;
    Audio.Rack?.port.postMessage({ Sport: On ? 0.05 : 0 });
    MarkPower();
}

/** Plays a pre-rendered aircraft clip (the propeller synthesis is too heavy to run on the page thread on demand). */
async function Fly(Name)
{
    await Wake();
    const Pad = document.querySelector(`[data-fly="${Name}"]`);
    if (!Audio.Buffers.has(Name))
    {
        const Bytes = await (await fetch(`../../../Exhibits/CabinHmi/Audio/${AircraftClips[Name]}.mp3`)).arrayBuffer();
        Audio.Buffers.set(Name, await Audio.Context.decodeAudioData(Bytes));
    }
    const Source = Audio.Context.createBufferSource();
    Source.buffer = Audio.Buffers.get(Name);
    Pad?.classList.add('Ring'); Source.onended = () => Pad?.classList.remove('Ring');
    Source.connect(Audio.Foley); Source.start();
    return Source.buffer.duration;
}

function MarkPower()
{
    for (const Pad of document.querySelectorAll('[data-induction]')) Pad.classList.toggle('Picked', (Pad.dataset.induction || null) === Induction && !Brands[CarName].Electric);
    for (const Pad of document.querySelectorAll('[data-car]')) Pad.classList.toggle('Picked', Pad.dataset.car === CarName);
    Find('SportSound')?.classList.toggle('On', SportSound);
    const Brand = Brands[CarName];
    Find('BrandSpec').textContent = Induction && !Brand.Electric ? `${Brand.Spec.replace(' · 100% synthesized', '')} · + ${Inducers[Induction].Label.toLowerCase()}` : Brand.Spec;
}

function PickSurface(Name)
{
    Controls.Surface = Name; SendControls();
    for (const Chip of document.querySelectorAll('.Surface')) Chip.classList.toggle('On', Chip.dataset.surface === Name);
}

function Flip(Id, Key, Options)
{
    const Button = Find(Id), Index = (Options.indexOf(Button.dataset.on) + 1) % Options.length, Choice = Options[Index];
    Button.dataset.on = Choice; Button.querySelector('b').textContent = Choice;
    if (Key === 'Pickup') Controls.Pickup = Choice;
    if (Key === 'Automatic') Controls.Automatic = Choice === 'Auto';
    if (Key === 'Traction') Controls.Traction = Choice === 'On';
    Wake().then(SendControls);
}

// Held inputs ramp like real pedals and a steering rack rather than switching instantly.
const Held = { Throttle: false, Brake: false, Left: false, Right: false };
let Forced = null;                                                     // test hook override { Control: Amount }
function RampInputs(Seconds)
{
    const Toward = (Current, Target, Up, Down) => Current < Target ? Math.min(Target, Current + Up * Seconds) : Math.max(Target, Current - Down * Seconds);
    const Before = JSON.stringify(Controls);
    Controls.Throttle = Forced?.Throttle ?? Toward(Controls.Throttle, Held.Throttle ? 1 : 0, 5, 8);
    Controls.Brake = Forced?.Brake ?? Toward(Controls.Brake, Held.Brake ? 1 : 0, 4, 8);
    Controls.Steer = Forced?.Steer ?? Toward(Controls.Steer, (Held.Right ? 0.75 : 0) - (Held.Left ? 0.75 : 0), 1.6, 2.4);
    if (JSON.stringify(Controls) !== Before) SendControls();
}

function BindInputs()
{
    const Keys = {
        KeyW: 'Throttle', ArrowUp: 'Throttle', KeyS: 'Brake', ArrowDown: 'Brake', KeyA: 'Left', ArrowLeft: 'Left', KeyD: 'Right', ArrowRight: 'Right',
    };
    addEventListener('keydown', (Event) =>
    {
        if (Event.target.closest?.('input')) return;
        Wake();
        if (Keys[Event.code]) { Held[Keys[Event.code]] = true; Event.preventDefault(); return; }
        if (Event.repeat) return;
        if (Event.code === 'Space') { Controls.Handbrake = true; SendControls(); Event.preventDefault(); }
        else if (Event.code === 'Enter') Command('Start');
        else if (Event.code === 'KeyE') Command('ShiftUp');
        else if (Event.code === 'KeyQ') Command('ShiftDown');
        else if (Event.code === 'KeyN') Command('Neutral');
        else if (Event.code === 'KeyC') Flip('Pickup', 'Pickup', ['Chase', 'Cabin']);
        else if (Event.code === 'KeyV') PickCar(CarOrder[(CarOrder.indexOf(CarName) + 1) % CarOrder.length]);
        else if (Event.code === 'KeyI') { const Names = [null, ...Object.keys(Inducers)]; PickInduction(Names[(Names.indexOf(Induction) + 1) % Names.length]); }
        else if (Event.code === 'KeyK') Kick('Kerb');
        else if (Event.code === 'KeyF') PullTrigger(Picked);
        else if (Event.code === 'KeyR') ToggleTurbo();
        else if (Event.code === 'KeyB') { Controls.TransBrake = true; SendControls(); }
        else if (Event.code === 'KeyP') Kick('Pothole');
        else if (Event.code === 'KeyM') SetRoadNoise(!RoadOn);
        else if (Event.code === 'KeyG') Flip('Shifting', 'Automatic', ['Auto', 'Manual']);
        else if (Event.code === 'KeyT') Flip('Traction', 'Traction', ['On', 'Off']);
        else if (/^Digit[1-8]$/.test(Event.code)) PickSurface(Object.keys(Surfaces)[+Event.code.slice(5) - 1]);
    });
    addEventListener('keyup', (Event) =>
    {
        if (Keys[Event.code]) Held[Keys[Event.code]] = false;
        if (Event.code === 'Space') { Controls.Handbrake = false; SendControls(); }
        if (Event.code === 'KeyB') { Controls.TransBrake = false; SendControls(); }
        if (Event.code === 'KeyF' && Trigger.Name) ReleaseTrigger();
    });
    addEventListener('blur', () => { for (const Key in Held) Held[Key] = false; ReleaseTrigger(); });
    addEventListener('pointerdown', () => Wake(), { once: true });
    for (const Pedal of document.querySelectorAll('[data-hold]'))
    {
        const Control = Pedal.dataset.hold;
        const Set = (Down) => { Held[Control] = Down; Pedal.classList.toggle('Held', Down); };
        Pedal.addEventListener('pointerdown', (Event) => { Pedal.setPointerCapture(Event.pointerId); Set(true); });
        Pedal.addEventListener('pointerup', () => Set(false));
        Pedal.addEventListener('pointercancel', () => Set(false));
    }
    Find('Start').onclick = () => Command('Start');
    Find('PaddleUp').onclick = () => Command('ShiftUp');
    Find('PaddleDown').onclick = () => Command('ShiftDown');
    Find('Neutral').onclick = () => Command('Neutral');
    Find('Pickup').onclick = () => Flip('Pickup', 'Pickup', ['Chase', 'Cabin']);
    Find('CarPick').onclick = () => PickCar(CarOrder[(CarOrder.indexOf(CarName) + 1) % CarOrder.length]);
    Find('Shifting').onclick = () => Flip('Shifting', 'Automatic', ['Auto', 'Manual']);
    Find('Traction').onclick = () => Flip('Traction', 'Traction', ['On', 'Off']);
    Find('LagScale').oninput = (Event) => SetLag(Event.target.value);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TACHOMETER
//------------------------------------------------------------------------------------------------------------------------

const Tach = Find('Tach').getContext('2d');
let ShownRpm = 0;
function DrawTach(Seconds)
{
    ShownRpm += (Readout.Rpm - ShownRpm) * Math.min(1, Seconds * 25);
    const Profile = Profiles[CarName], Size = 640, Centre = Size / 2, Radius = 268, Max = Brands[CarName].Dial;
    const From = Math.PI * 0.75, Sweep = Math.PI * 1.5, Angle = (Rpm) => From + Sweep * Math.min(1, Rpm / Max);
    Tach.clearRect(0, 0, Size, Size);
    Tach.lineCap = 'round';
    Tach.lineWidth = 18; Tach.strokeStyle = '#1c1d23';
    Tach.beginPath(); Tach.arc(Centre, Centre, Radius, From, From + Sweep); Tach.stroke();
    Tach.strokeStyle = 'rgba(255,43,28,0.35)';
    Tach.beginPath(); Tach.arc(Centre, Centre, Radius, Angle(Profile.Redline), From + Sweep); Tach.stroke();
    const Glow = Tach.createLinearGradient(0, Size, Size, 0);
    Glow.addColorStop(0, '#ffffff'); Glow.addColorStop(0.7, '#ffb02e'); Glow.addColorStop(1, '#ff2b1c');
    Tach.strokeStyle = ShownRpm > Profile.Redline ? '#ff2b1c' : Glow;
    Tach.shadowColor = 'rgba(255,90,40,0.6)'; Tach.shadowBlur = 18;
    if (ShownRpm > 20) { Tach.beginPath(); Tach.arc(Centre, Centre, Radius, From, Angle(ShownRpm)); Tach.stroke(); }
    Tach.shadowBlur = 0;
    Tach.fillStyle = '#9aa0ab'; Tach.font = '500 22px DM Sans'; Tach.textAlign = 'center'; Tach.textBaseline = 'middle';
    const Step = Max > 10000 ? 2 : 1;                                  // electric dials (16–18k) label every 2000
    for (let Thousand = 0; Thousand <= Max / 1000; Thousand += Step)
    {
        const At = Angle(Thousand * 1000), Cos = Math.cos(At), Sin = Math.sin(At);
        Tach.strokeStyle = Thousand * 1000 >= Profile.Redline ? '#ff2b1c' : '#4a4e58'; Tach.lineWidth = 3;
        Tach.beginPath(); Tach.moveTo(Centre + Cos * (Radius - 30), Centre + Sin * (Radius - 30)); Tach.lineTo(Centre + Cos * (Radius - 44), Centre + Sin * (Radius - 44)); Tach.stroke();
        Tach.fillStyle = Thousand * 1000 >= Profile.Redline ? '#ff6a5c' : '#9aa0ab';
        Tach.fillText(String(Thousand), Centre + Cos * (Radius - 70), Centre + Sin * (Radius - 70));
    }
    // Shift lights across the top: green, amber, red as the limiter nears.
    for (let Light = 0; Light < 10; Light++)
    {
        const Threshold = Profile.Limiter - 2750 + Light * 260, Lit = ShownRpm > Threshold;
        const Colour = Light < 4 ? '#3dff7a' : Light < 7 ? '#ffb02e' : '#ff2b1c';
        Tach.fillStyle = Lit ? (ShownRpm > Profile.Limiter - 60 && performance.now() % 120 < 60 ? '#ffffff' : Colour) : '#1c1d23';
        Tach.beginPath(); Tach.arc(Centre - 117 + Light * 26, 182, 7, 0, Math.PI * 2); Tach.fill();
    }
    Find('Gear').textContent = Readout.Gear ? String(Readout.Gear) : 'N';
    Find('Speed').textContent = Math.round(Readout.Kmh);
    Find('Rpm').textContent = `${Math.round(ShownRpm / 10) * 10} rpm`;
    Find('ChipLimiter').classList.toggle('On', !!Readout.Limiting);
    Find('ChipShift').classList.toggle('On', !!Readout.Shifting);
    Find('ChipSpin').classList.toggle('On', (Readout.Wheelspin ?? 0) > 0.05 || (Readout.Corner ?? 0) > 0.1 || (Readout.LockUp ?? 0) > 0.1);
    const Boosted = !Brands[CarName].Electric && (!!Profiles[CarName].Turbo || !!Profiles[CarName].Stock || !!Induction);
    MeasureSpool(Readout.Throttle ?? 0, Readout.Boost ?? 0, Boosted);
    const Assist = Boosted ? (Readout.Boost ?? 0) : (Readout.Motor ?? 0);
    Find('ChipKers').classList.toggle('On', Brands[CarName].Electric ? Assist < -0.05 : Math.abs(Assist) > (Boosted ? 0.6 : 0.05));
    Find('ChipAbs').classList.toggle('On', !!Readout.Abs);
    Find('Start').classList.toggle('Running', !!(Readout.Running || Readout.Starting));
    Find('Start').querySelector('b').textContent = Readout.Running || Readout.Starting ? 'STOP' : 'START';
    const Kers = Boosted ? (Readout.Boost ?? 0) * 2 - 1 : Assist, Bar = Find('KersBar');
    Bar.style.width = `${Math.abs(Kers) * 50}%`; Bar.style.left = Kers >= 0 ? '50%' : `${50 - Math.abs(Kers) * 50}%`;
    Bar.style.background = Kers >= 0 ? 'var(--Cyan)' : '#7dff9a';
    Find('PedalThrottle').querySelector('u').style.height = `${Controls.Throttle * 100}%`;
    Find('PedalBrake').querySelector('u').style.height = `${Controls.Brake * 100}%`;
    Find('SteerKnob').style.left = `${50 + Controls.Steer / 0.75 * 46}%`;
}

/**
 * Turbo lag stopwatch: from a stab of throttle (pedal past 90% with boost still under 30%) to boost passing 80% of full.
 * Belt-driven blowers read almost nothing; a big single from low rpm can read seconds or never get there.
 */
const Spool = { From: null, Shown: '' };
function MeasureSpool(Throttle, Boost, Boosted)
{
    const Readout = Find('SpoolTime');
    if (!Readout) return;
    if (!Boosted) { Spool.From = null; Readout.textContent = 'no boost on this car'; return; }
    const Now = performance.now();
    if (Spool.From === null && Throttle > 0.9 && Boost < 0.3) Spool.From = Now;
    if (Spool.From !== null && Throttle < 0.5) Spool.From = null;
    if (Spool.From !== null && Boost >= 0.8) { Spool.Shown = `last tip-in: ${((Now - Spool.From) / 1000).toFixed(2)} s to 80% boost`; Spool.From = null; }
    const Waiting = Spool.From !== null ? `spooling… ${((Now - Spool.From) / 1000).toFixed(1)} s` : '';
    Readout.textContent = `boost ${Math.round(Boost * 100)}% · ${Waiting || Spool.Shown || 'stab the throttle from low rpm to time the lag'}`;
}

function SetLag(Value)
{
    Controls.LagScale = Math.min(3, Math.max(0, +Value));
    Find('LagScale').value = Controls.LagScale; Find('LagShown').textContent = Controls.LagScale === 0 ? 'off' : `×${Controls.LagScale.toFixed(1)}`;
    SendControls();
}

let Last = performance.now();
function Tick(Now)
{
    const Seconds = Math.min(0.1, (Now - Last) / 1000); Last = Now;
    RampInputs(Seconds); DrawTach(Seconds);
    requestAnimationFrame(Tick);
}

BuildPanels();
BindInputs();
requestAnimationFrame(Tick);

window.Bench = {
    Wake, Command, Cue, RenderOffline, Readout: () => Readout, Car: PickCar, Kick, Fire, Turbo: ToggleTurbo,
    Induction: PickInduction, Sport: ToggleSport, Fly, Lag: SetLag, TransBrake: (On) => { Controls.TransBrake = !!On; SendControls(); },
    Hold: (Control, Amount) => { Forced = Amount === null ? null : { ...(Forced ?? {}), [Control]: Amount }; },
    Surface: PickSurface, Mix: SetLevel, RoadNoise: SetRoadNoise, Levels: () => ({ ...SentMix(), RoadOn }),
};
