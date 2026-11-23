// Ported from unassignedinbox/Slate@3c987f33c71c2477bd73ca29c69d5bd0ee484d1a
//============================================================================================================================================
//                                                         PLUMESOURCEPROJECTION.JS
//============================================================================================================================================
// 🧩 Vehicle telemetry projected into smoke, soot and fire sources, fire lights and ember emitters: onset hysteresis, burnout, burning rubber, pops.

// 📝 A port of Frontier's vehicle gas intake: SmokeOnsetClassifier.h decides whether a tyre is smoking (entry above
//    SourcePowerOnset sustained for SourceOnsetSeconds, release below SourceReleaseRatio of it after
//    SourceReleaseSeconds), and GasVehicleIntake.h ResolveWheelSmoke() turns the sliding power, the tread temperature
//    and the burning state into rates at the contact patch — smoke, heat and flame per second — with a share of the
//    patch velocity as injected momentum. The constants are SmokeSpecification's. What the exhibit adds:
//
//    | Source           | Where and why                                                                              |
//    |------------------|--------------------------------------------------------------------------------------------|
//    | patch            | Frontier's, plus the wheel's surface slip: a spinning tyre throws its smoke rearward along  |
//    |                  | the tread, which is what makes a burnout boil out behind the car instead of hovering        |
//    | arch             | a third of the patch's smoke carried round on the tread and shed behind and above the hub — |
//    |                  | the cloud that rolls out of the wheel arch                                                  |
//    | burning          | the flame wrapping the lower tyre, shedding black soot rather than white rubber smoke        |
//    | exhaust          | afterfire pops: short incandescent bursts thrown rearward with a dark puff                  |
//    | burst            | transient: a tyre letting go, fracture dust — queued with a duration and emitted until done |
//
//    Smoke follows the tread's heat: cold rubber barely smokes, a tread past 250 °C vapourises — the Warm factor.

import { AddVector, ScaleVector, MultiplyAddVector } from "./TransformProjection.js";

export const SmokeSpecification = Object.freeze({
    SourcePowerOnset:     200.0,     // [W]
    SourceReleaseRatio:   0.60,      // [-]
    SourceOnsetSeconds:   0.05,      // [s]
    SourceReleaseSeconds: 0.08,      // [s]
    SourcePowerFull:      18000.0,   // [W]
    MinimumSlipSpeed:     0.35,      // [m/s]
    DensityRate:          18.0,      // [1/s]
    CarrierVelocityShare: 0.15,      // [-]
    SmokeHeatRate:        0.04,      // [K/s]
    FireHeatPowerScale:   0.001,     // [K/J]
    PatchRadius:          0.16       // [m]   GasRoadTyrePatchRadius
});

export const SourceStride   = 12;   // [f32] PlumeSource in PlumeRecords.wgsl: position·radius, velocity·spare, rates (smoke, heat, flame, soot)
export const SourceCapacity = 24;   // [-]   four patches, four arches, eight flame seats, two exhaust tips, four bursts, two spare

const WarmBase       = 333.15;   // [K]   below this a sliding tread hardly smokes
const WarmSpan       = 190.0;    // [K]   from WarmBase to full vapourisation
const FlingShare     = 0.35;     // [-]   of the surface slip speed carried by the thrown smoke
const FlingCeiling   = 9.0;      // [m/s]
const ArchShare      = 0.35;     // [-]   of the patch smoke shed from the arch
const FireColour     = [1.0, 0.46, 0.14];   // [-] a sooty rubber flame, linear
const FireLightGain  = 0.00042;  // [lx·m²/W] combustion power to fire-light intensity
const FlameSidewallReach = 0.24; // [m]   hub to the flame seat outside the sidewall
const FlameRise      = 2.6;      // [m/s] momentum given to a flame seat — burning rubber jets its vapour upward

export class PlumeSourceProjection
{
    constructor()
    {
        this.Onsets      = [0, 1, 2, 3].map(() => ({ RequestSeconds: 0.0, QuietSeconds: 0.0, Active: false }));
        this.PopSeconds  = [0.0, 0.0];
        this.PopStrength = [0.0, 0.0];
        this.Seed        = 0x2545F491;
        this.Records     = new Float32Array(SourceCapacity * SourceStride);
        this.Count       = 0;
        this.ActiveTyres = 0;
        this.Popping     = false;
        this.Bursts      = [];
        this.Lights      = [];
        this.Embers      = [];
        this.Elapsed     = 0.0;
    }

    Reset()
    {
        for (const Onset of this.Onsets)
            Object.assign(Onset, { RequestSeconds: 0.0, QuietSeconds: 0.0, Active: false });
        this.PopSeconds = [0.0, 0.0];
        this.Bursts = [];
    }

    ProjectUniform()
    {
        this.Seed ^= this.Seed << 13;
        this.Seed ^= this.Seed >>> 17;
        this.Seed ^= this.Seed << 5;
        return (this.Seed >>> 0) / 4294967296.0;
    }

    // 📝 Queues a transient source: { Position, Radius, Velocity, Smoke, Heat, Flame, Soot, Seconds }.
    QueueBurst(Burst)
    {
        this.Bursts.push({ ...Burst, Remaining: Burst.Seconds });
        if (this.Bursts.length > 4)
            this.Bursts.shift();
    }

    // 📝 AdvanceSmokeOnset(): entry needs power and slip speed sustained, release has grace, contact loss cuts at once.
    AdvanceOnset(Onset, Power, SlipSpeed, Contact, Seconds)
    {
        const Specification = SmokeSpecification;
        if (!Contact || !(Seconds > 0.0))
        {
            Object.assign(Onset, { RequestSeconds: 0.0, QuietSeconds: 0.0, Active: false });
            return false;
        }
        const AboveEntry   = Power > Specification.SourcePowerOnset && SlipSpeed >= Specification.MinimumSlipSpeed;
        const AboveRelease = Power > Specification.SourcePowerOnset * Specification.SourceReleaseRatio && SlipSpeed >= Specification.MinimumSlipSpeed;
        if (AboveEntry)
        {
            Onset.RequestSeconds += Seconds;
            Onset.QuietSeconds = 0.0;
            if (Onset.RequestSeconds + 1.0e-6 >= Specification.SourceOnsetSeconds)
                Onset.Active = true;
        }
        else if (Onset.Active && AboveRelease)
            Onset.QuietSeconds = 0.0;
        else
        {
            Onset.RequestSeconds = 0.0;
            Onset.QuietSeconds += Seconds;
            if (Onset.QuietSeconds >= Specification.SourceReleaseSeconds)
                Onset.Active = false;
        }
        return Onset.Active;
    }

    // 📝 Fills Records with this advance's sources, Lights with the fire lights and Embers with the ember emitters.
    //    Options: { Afterfire, EmissionRate, SmokeYield, HeatYield, SootYield, FireLight, Embers }.
    Project(Telemetry, Seconds, Options)
    {
        const Specification = SmokeSpecification;
        const Rate = Options.EmissionRate ?? 1.0;
        const SmokeYield = (Options.SmokeYield ?? 1.0) * Rate, HeatYield = (Options.HeatYield ?? 1.0) * Rate;
        const SootYield = (Options.SootYield ?? 1.0) * Rate;
        this.Count = 0;
        this.ActiveTyres = 0;
        this.Lights = [];
        this.Embers = [];
        this.Elapsed += Seconds;
        const Emit = (Position, Radius, Velocity, Smoke, Heat, Flame, Soot) =>
        {
            if (this.Count >= SourceCapacity || (Smoke <= 0.0 && Heat <= 0.0 && Flame <= 0.0 && Soot <= 0.0))
                return;
            this.Records.set([Position[0], Position[1], Position[2], Radius, Velocity[0], Velocity[1], Velocity[2], 0.0,
                              Smoke, Heat, Flame, Soot], this.Count * SourceStride);
            this.Count++;
        };

        // ① tyres — ResolveWheelSmoke(), thrown along the tread's slip, shed again from the arch
        Telemetry.Wheels.forEach((Wheel, Index) =>
        {
            const Thermal  = Wheel.Thermal;
            const Burning  = Thermal.Ignited && Thermal.CombustionPower > 0.0;
            const Power    = Wheel.SlidingPower;
            const Sliding  = this.AdvanceOnset(this.Onsets[Index], Power, Wheel.SlipSpeed, Wheel.InContact, Seconds);
            if (!Sliding && !Burning)
                return;
            this.ActiveTyres++;
            const Anchor   = Burning && !Wheel.InContact ? Wheel.HubPosition : Wheel.ContactPoint;
            const Position = [Anchor[0], Anchor[1], Anchor[2] + 0.04];
            const Strength = Sliding ? Math.min(4.0, Math.max(0.0, (Power - Specification.SourcePowerOnset)
                                                              / Math.max(1.0, Specification.SourcePowerFull - Specification.SourcePowerOnset))) : 0.0;
            const Warm     = Math.min(1.0, Math.max(0.0, (Thermal.SurfaceTemperature - WarmBase) / WarmSpan));
            const Smoke    = Specification.DensityRate * Strength * (0.35 + 0.65 * Warm) * (Wheel.Burst ? 0.25 : 1.0);
            // 📝 The tread at the patch moves at (surface speed − ground speed) along the wheel's heading; smoke leaves
            //    with a share of that, rearward of a tyre spinning forward, plus a little lift.
            const SurfaceSlip = (Wheel.SurfaceSpeed || 0.0) - (Wheel.Longitudinal ? (Wheel.ContactVelocity[0] * Wheel.Longitudinal[0]
                              + Wheel.ContactVelocity[1] * Wheel.Longitudinal[1] + Wheel.ContactVelocity[2] * Wheel.Longitudinal[2]) : 0.0);
            const Fling = Math.max(-FlingCeiling, Math.min(FlingCeiling, FlingShare * SurfaceSlip));
            const Heading = Wheel.Longitudinal || [1.0, 0.0, 0.0];
            const Thrown = MultiplyAddVector(ScaleVector(Wheel.ContactVelocity, Specification.CarrierVelocityShare), Heading, -Fling);
            Emit(Position, Specification.PatchRadius * 1.4, AddVector(Thrown, [0.0, 0.0, 0.35]), Smoke * SmokeYield,
                 Specification.SmokeHeatRate * Strength * HeatYield, 0.0, 0.0);
            if (Sliding && Math.abs(Fling) > 1.0 && Wheel.Radius)
            {
                const Back = ScaleVector(Heading, -Math.sign(Fling));
                const Arch = AddVector(AddVector(Wheel.HubPosition, ScaleVector(Back, Wheel.Radius * 0.85)), [0.0, 0.0, Wheel.Radius * 0.35]);
                Emit(Arch, 0.28, AddVector(ScaleVector(Back, 0.45 * Math.abs(Fling)), [0.0, 0.0, 1.0]), ArchShare * Smoke * SmokeYield, 0.0, 0.0, 0.0);
            }
            // 📝 Burning rubber burns where air reaches it — the outer sidewall and the back of the tread — and climbs
            //    past the arch. Two flame seats per tyre, both in open air beside the body, so the lattice's velocity
            //    cells there are fluid and the column rises instead of pooling under the arch. Together they shed
            //    Frontier's 12/s of incandescence and black soot in place of the white rubber smoke.
            if (Burning)
            {
                const Heading = Wheel.Longitudinal || [1.0, 0.0, 0.0];
                const Outward = ScaleVector([-Heading[1], Heading[0], 0.0], (Index & 1) ? -1.0 : 1.0);
                const Radius = Wheel.Radius || 0.5;
                const Sidewall = AddVector(MultiplyAddVector(Wheel.HubPosition, Outward, FlameSidewallReach), [0.0, 0.0, -0.3 * Radius]);
                const Trailing = AddVector(MultiplyAddVector(Wheel.HubPosition, Heading, -(Radius + 0.12)), [0.0, 0.0, -0.15 * Radius]);
                const Carried = ScaleVector(Wheel.ContactVelocity, Specification.CarrierVelocityShare);
                const Heat = Thermal.CombustionPower * Specification.FireHeatPowerScale * HeatYield;
                Emit(Sidewall, 0.3, AddVector(MultiplyAddVector(Carried, Outward, 0.4), [0.0, 0.0, FlameRise]), 0.6 * SmokeYield, 0.6 * Heat,
                     7.0 * Rate, 8.0 * SootYield);
                Emit(Trailing, 0.28, AddVector(Carried, [0.0, 0.0, FlameRise]), 0.4 * SmokeYield, 0.4 * Heat, 5.0 * Rate, 6.0 * SootYield);
                const Flank = Sidewall;
                const Flicker = 0.78 + 0.22 * Math.sin(this.Elapsed * 17.0 + Index * 2.1) * Math.sin(this.Elapsed * 6.3 + Index);
                const Intensity = Thermal.CombustionPower * FireLightGain * (Options.FireLight ?? 1.0) * Flicker;
                this.Lights.push({ Position: AddVector(Flank, [0.0, 0.0, 0.45]), Radius: 0.4, Radiance: ScaleVector(FireColour, Intensity) });
                this.Embers.push({ Position: Flank, Velocity: AddVector(ScaleVector(Wheel.ContactVelocity, 0.3), [0.0, 0.0, 2.2]),
                                   Rate: 55.0 * (Options.Embers ?? 1.0), Temperature: 1350.0, Spread: 0.9, Lift: 2.5, Gravity: 0.15, Life: 1.6 });
            }
        });

        // ② exhaust afterfire: while the window is open each tip fires 35–90 ms bursts with random gaps
        this.Popping = false;
        for (let Tip = 0; Tip < Telemetry.Exhausts.length && Tip < 2; ++Tip)
        {
            this.PopSeconds[Tip] = Math.max(0.0, this.PopSeconds[Tip] - Seconds);
            if (Options.Afterfire && Telemetry.AfterfireSeconds > 0.0 && this.PopSeconds[Tip] <= 0.0 && this.ProjectUniform() < Seconds * 14.0)
            {
                this.PopSeconds[Tip]  = 0.035 + 0.055 * this.ProjectUniform();
                this.PopStrength[Tip] = 0.6 + 0.8 * this.ProjectUniform();
            }
            if (this.PopSeconds[Tip] <= 0.0)
                continue;
            this.Popping = true;
            const Exhaust  = Telemetry.Exhausts[Tip];
            const Strength = this.PopStrength[Tip];
            const Speed    = 8.0 + 6.0 * Strength;
            const Velocity = MultiplyAddVector(ScaleVector(Telemetry.Velocity, 0.7), Exhaust.Direction, Speed);
            const Nozzle   = MultiplyAddVector(Exhaust.Position, Exhaust.Direction, 0.15);
            Emit(Nozzle, 0.1, Velocity, 1.0 * Strength * SmokeYield, 260.0 * Strength * HeatYield, 40.0 * Strength * Rate, 1.2 * Strength * SootYield);
            this.Lights.push({ Position: MultiplyAddVector(Nozzle, Exhaust.Direction, 0.3), Radius: 0.25,
                               Radiance: ScaleVector([1.0, 0.55, 0.2], 9.0 * Strength * (Options.FireLight ?? 1.0)) });
            this.Embers.push({ Position: Nozzle, Velocity, Rate: 260.0 * Strength * (Options.Embers ?? 1.0), Temperature: 1700.0,
                               Spread: 1.6, Lift: 0.4, Gravity: 0.6, Life: 0.35 });
        }

        // ③ transient bursts until their time is spent
        for (const Burst of this.Bursts)
        {
            const Share = Math.min(1.0, Burst.Remaining / Math.max(Burst.Seconds, 1.0e-3));
            Emit(Burst.Position, Burst.Radius, Burst.Velocity, (Burst.Smoke || 0.0) * Share * Rate, (Burst.Heat || 0.0) * Share * HeatYield,
                 (Burst.Flame || 0.0) * Share * Rate, (Burst.Soot || 0.0) * Share * SootYield);
            Burst.Remaining -= Seconds;
        }
        this.Bursts = this.Bursts.filter((Burst) => Burst.Remaining > 0.0);
        this.Lights.sort((Left, Right) => (Right.Radiance[0] + Right.Radiance[1]) - (Left.Radiance[0] + Left.Radiance[1]));
        return this.Count;
    }

    // 📝 Bounds of every source grown by 3.5 of its radii (the Gaussian's reach), for the lattice's cheap reject.
    ResolveBounds()
    {
        const Minimum = [Infinity, Infinity, Infinity], Maximum = [-Infinity, -Infinity, -Infinity];
        for (let Index = 0; Index < this.Count; ++Index)
        {
            const Offset = Index * SourceStride;
            const Reach = Math.max(this.Records[Offset + 3], 0.3) * 3.5;
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                Minimum[Axis] = Math.min(Minimum[Axis], this.Records[Offset + Axis] - Reach);
                Maximum[Axis] = Math.max(Maximum[Axis], this.Records[Offset + Axis] + Reach);
            }
        }
        return { Minimum, Maximum };
    }
}
