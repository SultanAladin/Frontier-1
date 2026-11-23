// VOICE RACK WORKLET
// 🧩 AudioWorklet that runs the live car on the audio thread: TractionIntegrator steps every 128-sample block and feeds
//    PulseTrainEngine (LaFerrari or GT-R NISMO, optionally with an InductionVoice turbo or supercharger on top) or, for
//    the electric cars (Taycan Turbo S, Next-Gen racer), ElectricIntegrator feeds DriveUnit; then SurfaceRoll,
//    TyreSqueal, BrakeRotor and SuspensionTravel, the same classes the offline reel renders with.
// 📝 The page posts { Controls }, { Command }, { Mix }, { Car } and { Induction } messages (processorOptions { Start,
//    Throttle, Car, Induction, Gear, Mix } preset them); Mix.Road and Mix.Wind set SurfaceRoll's road and wind layers
//    separately. The worklet posts a telemetry readout ~30 times a second. Registered as 'voice-rack'.

import { PulseTrainEngine, Profiles } from './PulseTrainEngine.js';
import { TractionIntegrator } from './TractionIntegrator.js';
import { SurfaceRoll, TyreSqueal, Surfaces } from './ContactPatch.js';
import { BrakeRotor, SuspensionTravel } from './ChassisVoices.js';
import { ElectricCars, ElectricIntegrator, DriveUnit } from './ElectricDrive.js';
import { Inducers, InductedProfile, InductionVoice } from './ForcedInduction.js';

const Knee = (Sample) => { const Size = Math.abs(Sample); return Size <= 0.6 ? Sample : Math.sign(Sample) * (0.6 + 0.4 * Math.tanh((Size - 0.6) / 0.4)); };

class VoiceRack extends AudioWorkletProcessor
{
    constructor({ processorOptions = {} } = {})
    {
        super();
        const Rate = sampleRate;
        this.Pickup = 'Chase';
        this.Induction = Inducers[processorOptions.Induction] ? processorOptions.Induction : null;
        this.Choose(processorOptions.Car ?? 'LaFerrari');
        this.Roll = new SurfaceRoll(Rate, { Pickup: 'Chase' });
        this.Squeal = new TyreSqueal(Rate, { Pickup: 'Chase' });
        this.Brakes = new BrakeRotor(Rate, { Pickup: 'Chase' });
        this.Springs = new SuspensionTravel(Rate, { Pickup: 'Chase' });
        this.Surface = 'Asphalt'; this.Kick = 0;
        this.Levels = { Engine: 1, Road: 0.4, Wind: 0.8, Tyres: 1, Chassis: 1 };
        this.Roll.RoadLevel = this.Levels.Road; this.Roll.WindLevel = this.Levels.Wind;   // bench defaults (the page sends its mix on wake)
        this.Scratch = [new Float32Array(128), new Float32Array(128)];
        this.Clock = 0;
        this.port.onmessage = (Message) => this.Receive(Message.data);
        // Start-up inputs arrive with construction (offline renders cannot rely on port timing).
        if (processorOptions.Throttle) this.Car.Throttle = processorOptions.Throttle;
        if (processorOptions.Start) this.Car.Start();
        if (processorOptions.Gear) this.Car.Gear = processorOptions.Gear;
        if (processorOptions.Mix) this.Receive({ Mix: processorOptions.Mix });
        this.Fault = null;
    }

    /** Swaps the car (engine voice and integrator); the new one waits switched off, like walking up to it. */
    Choose(Name)
    {
        this.CarName = Profiles[Name] || ElectricCars[Name] ? Name : 'LaFerrari';
        this.Inducer = null;
        if (ElectricCars[this.CarName])
        {
            const Profile = ElectricCars[this.CarName];
            this.Car = new ElectricIntegrator(Profile);
            this.Engine = new DriveUnit(sampleRate, Profile, { Pickup: this.Pickup, Sport: this.Sport ?? 0 });
            return;
        }
        // Combustion: an inducer replaces the car's own induction (the GT-R's twin turbos go silent and its curve is redone).
        const Base = Profiles[this.CarName], Profile = this.Induction ? InductedProfile(Base, this.Induction) : Base;
        this.Car = new TractionIntegrator(Profile);
        this.Car.LagScale = this.LagScale ?? 1;
        this.Engine = new PulseTrainEngine(sampleRate, Profile, { Pickup: this.Pickup });
        // A car with a stock blower (the Demon) keeps its own voice unless an inducer swap replaces it.
        const Voice = this.Induction ?? Base.Stock ?? null;
        if (Voice) this.Inducer = new InductionVoice(sampleRate, Voice, { Pickup: this.Pickup, Redline: Profile.Redline });
    }

    /** Swaps the inducer and keeps the car running if it was (a workshop swap, done instantly). */
    Induce(Name)
    {
        this.Induction = Inducers[Name] ? Name : null;
        if (ElectricCars[this.CarName]) return;
        const Old = this.Car;
        this.Choose(this.CarName);
        if (this.Engine.Rpm) this.Engine.Rpm.Current = Old.Rpm;
        Object.assign(this.Car, { Running: Old.Running, Rpm: Old.Rpm, Speed: Old.Speed, Gear: Old.Gear, LastGear: Old.Gear, Throttle: Old.Throttle, Automatic: Old.Automatic, Traction: Old.Traction });
    }

    Receive({ Controls, Command, Mix, Car: Chosen, Induction, Sport })
    {
        if (Sport !== undefined) { this.Sport = Sport; if (this.Engine instanceof DriveUnit) this.Engine.Sport = Sport; }
        if (Chosen) this.Choose(Chosen);
        if (Induction !== undefined) this.Induce(Induction);
        const Car = this.Car;
        if (Controls)
        {
            for (const Key of ['Throttle', 'Brake', 'Steer', 'Handbrake', 'Automatic', 'Traction', 'TransBrake']) if (Key in Controls) Car[Key] = Controls[Key];
            if (Controls.Surface && Surfaces[Controls.Surface]) this.Surface = Controls.Surface;
            if ('LagScale' in Controls) this.LagScale = Car.LagScale = Math.min(4, Math.max(0, Controls.LagScale));
            if (Controls.Pickup) { this.Pickup = this.Engine.Pickup = this.Roll.Pickup = this.Squeal.Pickup = this.Brakes.Pickup = this.Springs.Pickup = Controls.Pickup; if (this.Inducer) this.Inducer.Pickup = Controls.Pickup; }
        }
        if (Mix)
        {
            Object.assign(this.Levels, Mix);
            // Road and Wind are separate layers inside SurfaceRoll (road roar can be switched off and wind kept).
            if (Mix.Road !== undefined) this.Roll.RoadLevel = Mix.Road;
            if (Mix.Wind !== undefined) this.Roll.WindLevel = Mix.Wind;
        }
        if (Command === 'Start') Car.Running ? Car.Stop() : Car.Start();
        if (Command === 'ShiftUp') Car.ShiftUp();
        if (Command === 'ShiftDown') Car.ShiftDown();
        if (Command === 'Neutral') Car.Gear = 0;
        if (Command === 'Kerb') this.Kick = 0.09;
        if (Command === 'Pothole') this.Kick = -0.08;
    }

    /** Runs one block; a fault in any voice is reported once and the block is silenced instead of killing the node. */
    process(Inputs, Outputs)
    {
        try { return this.Block(Outputs); }
        catch (Error)
        {
            Outputs[0].forEach((Channel) => Channel.fill(0));
            if (!this.Fault) { this.Fault = String(Error?.stack ?? Error); this.port.postMessage({ Fault: this.Fault }); }
            return true;
        }
    }

    Block(Outputs)
    {
        const [Left, Right = Left] = Outputs[0];
        const Size = Left.length;
        const Car = this.Car, Levels = this.Levels, [ScratchL, ScratchR] = this.Scratch;
        const Feed = Car.Step(Size / sampleRate);
        this.Engine.Feed(Feed);
        this.Inducer?.Feed(Feed);
        this.Roll.Feed({ Speed: Car.Speed, Surface: this.Surface });
        this.Squeal.Feed({ Speed: Car.Speed, Corner: Car.Corner, LockUp: Car.LockUp, Wheelspin: Car.Wheelspin, Grip: Surfaces[this.Surface].Grip, Surface: this.Surface });
        this.Brakes.Feed({ Speed: Car.Speed, Brake: Car.Brake, Abs: Car.AbsActive, Handbrake: Car.Handbrake });
        this.Springs.Feed({ Speed: Car.Speed, Surface: this.Surface, Accel: Car.Accel, Lateral: Car.Lateral, Kick: this.Kick });
        this.Kick = 0;
        Left.fill(0); Right.fill(0);
        for (const [Voice, Level] of [[this.Engine, Levels.Engine], ...(this.Inducer ? [[this.Inducer, Levels.Engine]] : []), [this.Roll, 1], [this.Squeal, Levels.Tyres], [this.Brakes, Levels.Chassis], [this.Springs, Levels.Chassis]])
        {
            ScratchL.fill(0); ScratchR.fill(0);
            Voice.Render(ScratchL, ScratchR, 0, Size);
            for (let Index = 0; Index < Size; Index++) { Left[Index] += ScratchL[Index] * Level; Right[Index] += ScratchR[Index] * Level; }
        }
        // Safety knee: pops and the limiter can spike past full scale; above 0.6 the peaks fold softly towards 1.
        for (let Index = 0; Index < Size; Index++) { Left[Index] = Knee(Left[Index]); Right[Index] = Knee(Right[Index]); }
        if ((this.Clock += Size) >= sampleRate / 30) { this.Clock = 0; this.port.postMessage({ ...Car.Readout(), Induction: this.Induction, Stock: Profiles[this.CarName]?.Stock ?? null }); }
        return true;
    }
}

registerProcessor('voice-rack', VoiceRack);
