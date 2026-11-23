//============================================================================================================================================
//                                                           TYRETHERMALSOLVER.JS
//============================================================================================================================================
// 🧩 Per-wheel tread, carcass and gas heat with sustained-overheat ignition, the drift-smoke onset and the wheel's smoke and fire source.

// 📝 A port of Frontier's ThermalIntegrator.h (three coupled heat capacities, implicit conduction, sustained-overheat
//    combustion, conditioned grip), SmokeOnsetClassifier.h (sliding-source hysteresis) and GasVehicleIntake.h
//    ResolveWheelSmoke() (combined sliding work into a world-space drift/fire source), with the authored defaults.
//    What differs: there is no XPBD carcass here, so the enclosed volume is the rest volume and the gas law reads
//    pressure from gas temperature alone. Sliding heat is the actual contact dissipation |Fx·Vsx| + |Fy·Vsy| the
//    vehicle solver integrates across its substeps; static support forces and impacts never heat the tyre.

export const ThermalSpecification = Object.freeze({
    AmbientTemperature:        293.15,       // [K]
    RoadTemperature:           293.15,       // [K]
    SurfaceHeatCapacity:       1800.0,       // [J/K]   fast tread layer
    CarcassHeatCapacity:       14400.0,      // [J/K]   bulk rubber
    SurfaceCarcassConductance: 28.0,         // [W/K]
    CarcassGasConductance:     3.0,          // [W/K]
    SurfaceAirConductance:     12.0,         // [W/K]   still air
    CarcassAirConductance:     3.0,          // [W/K]
    RoadConductance:           18.0,         // [W/K]   contact cooling
    SlidingHeatShare:          0.75,         // [-]     sliding work entering rubber; the rest heats the road
    AirSpecificGasConstant:    287.05,       // [J/kg/K]
    AirSpecificHeat:           718.0,        // [J/kg/K]
    OptimumTemperature:        353.15,       // [K]     grip plateau end
    GripFadeTemperature:       473.15,       // [K]     grip reaches its floor
    MinimumGripFactor:         0.60,         // [-]
    PressureGripSensitivity:   0.30,         // [-]     per fractional deviation from cold pressure
    CombustibleMass:           2.0,          // [kg]
    BurnRate:                  0.002,        // [kg/s]
    HeatOfCombustion:          30000000.0,   // [J/kg]  authored heat release, not chemical validation
    CombustionHeatShare:       0.10,         // [-]     burn heat entering the tyre; the rest feeds the plume
    RestVolume:                0.034,        // [m³]    enclosed air of the authored tyre
    ColdGaugePressure:         220000.0,     // [Pa]
    AtmosphericPressure:       101325.0      // [Pa]
});

// 📝 Wear and failure, outside Frontier's specification (the GI-Updated-Html-Version branch's AbrasionSpecification,
//    with two failure modes added). Sliding work wears tread off — faster when the rubber is hot — and the share that
//    leaves as smoke carries its pyrolysis heat away. A tyre lets go when its tread is worn through, when its carcass
//    cooks past CarcassFailure, or when hot gas drives the pressure past BurstPressure; a blowout empties it in a
//    fraction of a second, a nail (a puncture, or a pinch cut) in about twenty.
export const AbrasionSpecification = Object.freeze({
    TreadMass:          1.1,        // [kg]    wearable tread rubber per tyre
    WearCoefficient:    6.0e-8,     // [kg/J]  tread worn per joule of sliding work at the optimum temperature
    WearHeatSpan:       80.0,       // [K]     each span above the optimum adds the cold wear rate again
    VapourShare:        0.55,       // [-]     worn rubber that leaves as smoke rather than as crumbs
    PyrolysisHeat:      1.6e6,      // [J/kg]  carried away by the vapourised share
    BlowoutRate:        30.0,       // [1/s]   the air's escape when the tyre lets go
    PunctureRate:       0.035,      // [1/s]   a nail: half the pressure in about twenty seconds
    CarcassFailure:     493.15,     // [K]     220 °C bulk rubber: the cords' bond gives way
    BurstPressure:      650000.0    // [Pa]    gauge; ~3× a road tyre's rating
});

export const BurstCauses = Object.freeze({ Worn: "tread worn through", Cooked: "carcass overheated", Pressure: "over-pressure",
                                           Blowout: "blowout", Puncture: "puncture", Pinch: "pinch cut" });

// 📝 Authored is ThermalIntegrator.h's default: 623.15 K held for 4 s, extinguished below 473.15 K. Diagnostic is the
//    labelled shortened test setting (the C++ plan's "diagnostic overrides stay labelled"): any sustained drift lights it.
export const IgnitionModes = Object.freeze([
    { Caption: "Authored (350 °C held 4 s)",    Enabled: true,  Ignition: 623.15, Exposure: 4.0, Extinction: 473.15 },
    { Caption: "Diagnostic (160 °C held 1 s)",  Enabled: true,  Ignition: 433.15, Exposure: 1.0, Extinction: 373.15 },
    { Caption: "Off",                           Enabled: false, Ignition: 623.15, Exposure: 4.0, Extinction: 473.15 }
]);

// 📝 SmokeSequence.h SmokeSpecification and GasVehicleIntake.h constants. Rates are densities per second at the
//    source centre; the lattice converts them per tick.
export const SmokeSpecification = Object.freeze({
    SourcePowerOnset:      200.0,     // [W]    sliding must dissipate this much to request smoke
    SourceReleaseRatio:    0.60,      // [-]    an active source releases below this fraction of the onset
    SourceOnsetSeconds:    0.05,      // [s]    rejects single-tick friction spikes
    SourceReleaseSeconds:  0.08,      // [s]    keeps consent through brief oscillations
    SourcePowerFull:       18000.0,   // [W]    reference power for source strength
    MinimumSlipSpeed:      0.35,      // [m/s]  static support never smokes
    DensityRate:           18.0,      // [1/s]
    CarrierVelocityShare:  0.15,      // [-]    injection momentum from the contact's travel
    TreadEjectionShare:   0.10,      // [-]    share of the tread's slip velocity the shed smoke leaves with
    SmokeHeatRate:         0.04,      // [K/s]  faint buoyancy of friction smoke
    FireHeatPowerScale:    0.001,     // [K/J]  plume heat per watt of combustion
    FireSmokeRate:         12.0,      // [1/s]
    FireIncandescenceRate: 12.0,      // [1/s]  combustion provenance; friction smoke never glows
    PatchRadius:           0.16,      // [m]    half the contact width of a road tyre
    SmokeRadius:           0.30,      // [m]    friction-smoke footprint (≈ tyre width; C++ floors its sources at 0.6 m)
    FireRadius:            0.45,      // [m]    a burning tyre's footprint
    SmokeExpansion:        2.5,       // [1/s]  volumetric expansion of hot friction gas per unit strength (capped at 2)
    FireExpansion:         5.0,       // [1/s]  volumetric expansion of combustion gas
    PlumeLift:             0.55,      // [m]    the soot plume rises from above the flame, so the flame core stays visible
    PlumeHeatShare:        0.35,      // [-]    of the combustion plume heat carried by the soot plume
    SootRate:              18.0       // [1/s]  black combustion soot in the plume
});

export class TyreThermalSolver
{
    constructor(WheelCount = 4)
    {
        this.WheelCount = WheelCount;
        this.ColdPressures = new Array(WheelCount).fill(ThermalSpecification.ColdGaugePressure);
        this.Options = { Wear: true, WearScale: 1.0, Failure: true };
        this.Events = [];
        this.Reset();
    }

    // 📝 Fresh, cold tyres at the set cold pressures.
    Reset()
    {
        const S = ThermalSpecification;
        this.Wheels = Array.from({ length: this.WheelCount }, (_, Index) => ({
            Surface: S.AmbientTemperature, Carcass: S.AmbientTemperature, Gas: S.AmbientTemperature,
            AirMass: ResolveAirMass(this.ColdPressures[Index], S.AmbientTemperature), GaugePressure: this.ColdPressures[Index], Grip: 1.0,
            SlidingPower: 0.0, CombustionPower: 0.0, RemainingRubber: S.CombustibleMass, OverheatExposure: 0.0, Ignited: false,
            TreadMass: AbrasionSpecification.TreadMass, WearRate: 0.0, VapourRate: 0.0, LeakRate: 0.0, Burst: false, Cause: "",
            Onset: { RequestSeconds: 0.0, QuietSeconds: 0.0, Active: false }
        }));
    }

    // 📝 The tyre bay: cold pressures [Pa gauge] per wheel; every intact tyre is pumped to them now at its present gas
    //    temperature and any slow leak is sealed. A burst tyre needs a fresh one (Reset, or a rez).
    Inflate(Pressures)
    {
        this.ColdPressures = Pressures.slice();
        this.Wheels.forEach((Wheel, Index) =>
        {
            if (Wheel.Burst)
                return;
            Wheel.LeakRate = 0.0;
            Wheel.Cause = "";
            Wheel.AirMass = ResolveAirMass(this.ColdPressures[Index], Wheel.Gas);
            Wheel.GaugePressure = this.ColdPressures[Index];
        });
    }

    // 📝 A nail (a slow leak) or a blowout in one tyre; Cause names it for the readouts.
    Puncture(Index, Blowout = false, Cause = Blowout ? BurstCauses.Blowout : BurstCauses.Puncture)
    {
        const Wheel = this.Wheels[Index];
        if (!Wheel || Wheel.Burst)
            return false;
        Wheel.LeakRate = Math.max(Wheel.LeakRate, Blowout ? AbrasionSpecification.BlowoutRate : AbrasionSpecification.PunctureRate);
        Wheel.Cause = Cause;
        if (Blowout)
        {
            Wheel.Burst = true;
            this.Events.push({ Wheel: Index, Cause, Blowout: true });
        }
        return true;
    }

    // 📝 The frame's burst events (once each), for the pop, the debris and the announcement.
    DrainEvents()
    {
        const Events = this.Events;
        this.Events = [];
        return Events;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 HEAT TRANSFER
    //--------------------------------------------------------------------------------------------------------------------

    // 📐 ThermalIntegrator.h IntegrateThermal(): combustion gate, then the 3×3 backward-Euler conduction system
    //    (tread ↔ carcass ↔ gas, tread → air and road, carcass → air) solved by Gauss–Jordan elimination.
    //    Readings: per wheel { SlidingPower [W], ContactFraction [-], Airspeed [m/s] }; Mode: an IgnitionModes entry.
    Advance(Readings, Seconds, Mode)
    {
        if (!(Seconds > 0.0))
            return;
        const S = ThermalSpecification;
        this.Wheels.forEach((Wheel, Index) =>
        {
            const Reading = Readings[Index];
            const InContact = Reading.ContactFraction > 0.5;
            Wheel.SlidingPower = InContact ? Math.max(0.0, Reading.SlidingPower) : 0.0;
            Wheel.CombustionPower = 0.0;
            if (Mode.Enabled && Wheel.Surface >= Mode.Ignition)
                Wheel.OverheatExposure += Seconds;
            else
                Wheel.OverheatExposure = 0.0;
            if (Mode.Enabled && Wheel.RemainingRubber > 0.0 && Wheel.OverheatExposure >= Mode.Exposure)
                Wheel.Ignited = true;
            if (!Mode.Enabled || Wheel.RemainingRubber <= 0.0 || Wheel.Surface < Mode.Extinction)
                Wheel.Ignited = false;
            if (Wheel.Ignited)
            {
                const Burned = Math.min(Wheel.RemainingRubber, S.BurnRate * Seconds);
                Wheel.RemainingRubber -= Burned;
                Wheel.CombustionPower = Burned * S.HeatOfCombustion / Seconds;
            }
            // 📝 Abrasion: the tread wears with the sliding work, hotter rubber faster; the vapourised share leaves as
            //    smoke and takes its pyrolysis heat with it. A bare rim has no rubber left to heat or wear.
            if (Reading.Shredded)
                Wheel.SlidingPower = 0.0;
            const Abrasion = AbrasionSpecification;
            const HeatFactor = 1.0 + Math.max(0.0, Wheel.Surface - S.OptimumTemperature) / Abrasion.WearHeatSpan;
            Wheel.WearRate = this.Options.Wear && !Wheel.Burst && Wheel.TreadMass > 0.0
                           ? Abrasion.WearCoefficient * this.Options.WearScale * Wheel.SlidingPower * HeatFactor : 0.0;
            const Worn = Math.min(Wheel.TreadMass, Wheel.WearRate * Seconds);
            Wheel.TreadMass -= Worn;
            Wheel.VapourRate = Worn * Abrasion.VapourShare / Seconds;
            const VapourHeat = Worn * Abrasion.VapourShare * Abrasion.PyrolysisHeat;
            const SlidingHeat = Math.max(0.0, Wheel.SlidingPower * S.SlidingHeatShare * Seconds - VapourHeat);
            const BurnHeat = Wheel.CombustionPower * S.CombustionHeatShare * Seconds;
            const AirFactor = 1.0 + 0.35 * Math.sqrt(Math.max(0.0, Reading.Airspeed));
            const AirSurface = S.SurfaceAirConductance * AirFactor;
            const AirCarcass = S.CarcassAirConductance * AirFactor;
            const Road = InContact ? S.RoadConductance : 0.0;
            const Capacity = [S.SurfaceHeatCapacity, S.CarcassHeatCapacity, Wheel.AirMass * S.AirSpecificHeat];
            const Equations = [
                [Capacity[0] / Seconds + AirSurface + Road + S.SurfaceCarcassConductance, -S.SurfaceCarcassConductance, 0.0,
                 Capacity[0] * Wheel.Surface / Seconds + (SlidingHeat + BurnHeat) / Seconds + AirSurface * S.AmbientTemperature
                 + Road * S.RoadTemperature],
                [-S.SurfaceCarcassConductance, Capacity[1] / Seconds + AirCarcass + S.SurfaceCarcassConductance + S.CarcassGasConductance,
                 -S.CarcassGasConductance, Capacity[1] * Wheel.Carcass / Seconds + AirCarcass * S.AmbientTemperature],
                [0.0, -S.CarcassGasConductance, Capacity[2] / Seconds + S.CarcassGasConductance, Capacity[2] * Wheel.Gas / Seconds]
            ];
            for (let Pivot = 0; Pivot < 3; ++Pivot)
            {
                const Diagonal = Equations[Pivot][Pivot];
                for (let Column = Pivot; Column < 4; ++Column)
                    Equations[Pivot][Column] /= Diagonal;
                for (let Row = 0; Row < 3; ++Row)
                {
                    if (Row === Pivot)
                        continue;
                    const Factor = Equations[Row][Pivot];
                    for (let Column = Pivot; Column < 4; ++Column)
                        Equations[Row][Column] -= Factor * Equations[Pivot][Column];
                }
            }
            Wheel.Surface = Equations[0][3];
            Wheel.Carcass = Equations[1][3];
            Wheel.Gas = Equations[2][3];
            // 📝 A leak bleeds the air toward the mass that would sit at atmospheric pressure at this gas temperature.
            if (Wheel.LeakRate > 0.0)
            {
                const Settled = ResolveAirMass(0.0, Wheel.Gas);
                Wheel.AirMass = Settled + (Wheel.AirMass - Settled) * Math.exp(-Wheel.LeakRate * Seconds);
            }
            Wheel.GaugePressure = Math.max(0.0, Wheel.AirMass * S.AirSpecificGasConstant * Wheel.Gas / S.RestVolume - S.AtmosphericPressure);
            // 📝 Failure: worn through, cooked, or over-pressured — each a blowout, reported once.
            if (this.Options.Failure && !Wheel.Burst && !Reading.Shredded)
            {
                const Cause = this.Options.Wear && Wheel.TreadMass <= 0.0 ? BurstCauses.Worn
                            : Wheel.Carcass >= Abrasion.CarcassFailure ? BurstCauses.Cooked
                            : Wheel.GaugePressure >= Abrasion.BurstPressure ? BurstCauses.Pressure : "";
                if (Cause)
                    this.Puncture(Index, true, Cause);
            }
            const Hot = Math.min(1.0, Math.max(0.0, (Wheel.Surface - S.OptimumTemperature) / Math.max(1.0, S.GripFadeTemperature - S.OptimumTemperature)));
            const Reference = Math.max(1000.0, this.ColdPressures[Index]);
            const Deviation = Math.min(1.0, Math.abs(Wheel.GaugePressure - Reference) / Reference);
            Wheel.Grip = (1.0 - Hot * (1.0 - S.MinimumGripFactor)) * Math.max(0.25, 1.0 - S.PressureGripSensitivity * Deviation);
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    EMISSION
    //--------------------------------------------------------------------------------------------------------------------

    // 📐 SmokeOnsetClassifier.h AdvanceSmokeOnset(): entry above the onset power for SourceOnsetSeconds, hold above
    //    the release ratio, quiet for SourceReleaseSeconds before letting go; contact loss cuts friction at once.
    AdvanceOnset(Wheel, Reading, Seconds)
    {
        const S = SmokeSpecification;
        const Onset = Wheel.Onset;
        const InContact = Reading.ContactFraction > 0.5;
        if (!InContact || !(Seconds > 0.0))
        {
            Onset.RequestSeconds = 0.0;
            Onset.QuietSeconds = 0.0;
            Onset.Active = false;
            return false;
        }
        const AboveEntry = Wheel.SlidingPower > S.SourcePowerOnset && Reading.SlipSpeed >= S.MinimumSlipSpeed;
        const AboveRelease = Wheel.SlidingPower > S.SourcePowerOnset * S.SourceReleaseRatio && Reading.SlipSpeed >= S.MinimumSlipSpeed;
        if (AboveEntry)
        {
            Onset.RequestSeconds += Seconds;
            Onset.QuietSeconds = 0.0;
            if (Onset.RequestSeconds + 1.0e-6 >= S.SourceOnsetSeconds)
                Onset.Active = true;
        }
        else if (Onset.Active && AboveRelease)
        {
            Onset.QuietSeconds = 0.0;
        }
        else
        {
            Onset.RequestSeconds = 0.0;
            Onset.QuietSeconds += Seconds;
            if (Onset.QuietSeconds >= S.SourceReleaseSeconds)
                Onset.Active = false;
        }
        return Onset.Active;
    }

    // 📐 GasVehicleIntake.h ResolveWheelSmoke(): strength from the sliding power above the (released) onset, warmed by
    //    tread temperature; a burning tyre adds smoke, soot, incandescence and plume heat, and burns on in the air at
    //    the hub. Readings carry ContactPoint, HubPosition and ContactVelocity [world].
    ResolveEmissions(Readings, Seconds)
    {
        const S = SmokeSpecification;
        return this.Wheels.map((Wheel, Index) =>
        {
            const Reading = Readings[Index];
            const Sliding = this.AdvanceOnset(Wheel, Reading, Seconds);
            const Burning = Wheel.Ignited && Wheel.CombustionPower > 0.0;
            const InContact = Reading.ContactFraction > 0.5;
            const Onset = S.SourcePowerOnset * S.SourceReleaseRatio;
            const Strength = Sliding && InContact && Reading.SlipSpeed >= S.MinimumSlipSpeed && Wheel.SlidingPower > Onset
                           ? Math.min(4.0, Math.max(0.0, (Wheel.SlidingPower - Onset) / Math.max(1.0, S.SourcePowerFull - Onset))) : 0.0;
            const Warm = Math.min(1.0, Math.max(0.0, (Wheel.Surface - ThermalSpecification.AmbientTemperature) / 100.0));
            const Anchor = Burning && !InContact ? Reading.HubPosition : Reading.ContactPoint;
            const Velocity = Reading.ContactVelocity;
            const FireHeat = Burning ? Wheel.CombustionPower * S.FireHeatPowerScale : 0.0;
            const Carrier = [Velocity[0] * S.CarrierVelocityShare, Velocity[1] * S.CarrierVelocityShare,
                             Velocity[2] * S.CarrierVelocityShare + (Burning ? 1.2 : 0.15)];
            // 📝 A burning tyre is two sources: the flame (incandescence and most of the plume heat) wrapped round the
            //    hub, and the soot plume a little above it — friction smoke stays at the contact patch.
            const Tread = Reading.TreadVelocity || [0.0, 0.0, 0.0];
            return {
                Tread:     [Tread[0] * S.TreadEjectionShare, Tread[1] * S.TreadEjectionShare, 0.0],
                Active:    Strength > 0.0 || Burning,
                Burning,
                Strength,
                Position:  Burning ? [Reading.HubPosition[0], Reading.HubPosition[1], Reading.HubPosition[2] - 0.1]
                                   : [Anchor[0], Anchor[1], Anchor[2] + 0.04],
                Hub:       Reading.HubPosition,
                Radius:    Burning ? S.FireRadius : S.SmokeRadius,
                Expansion: S.SmokeExpansion * Math.min(Strength, 2.0) + (Burning ? S.FireExpansion : 0.0),
                SmokeRate: S.DensityRate * Strength * (0.5 + 0.5 * Warm),
                SootRate:  0.0,
                FlameRate: Burning ? S.FireIncandescenceRate : 0.0,
                HeatRate:  S.SmokeHeatRate * Strength + FireHeat * (1.0 - S.PlumeHeatShare),
                Velocity:  Carrier,
                Plume:     Burning ? { Offset: [0.0, 0.0, S.PlumeLift + 0.1], Radius: S.FireRadius, SmokeRate: S.FireSmokeRate,
                                       SootRate: S.SootRate, HeatRate: FireHeat * S.PlumeHeatShare, Expansion: 0.5 * S.FireExpansion }
                                   : null,
                CombustionPower: Wheel.CombustionPower
            };
        });
    }

    QueryBurning()
    {
        return this.Wheels.some((Wheel) => Wheel.Ignited);
    }

    QueryTreadFractions()
    {
        return this.Wheels.map((Wheel) => Math.max(0.0, Wheel.TreadMass / AbrasionSpecification.TreadMass));
    }
}

// 📐 Air mass [kg] that sits at Gauge [Pa] in the rest volume at Temperature [K].
function ResolveAirMass(Gauge, Temperature)
{
    const S = ThermalSpecification;
    return (Gauge + S.AtmosphericPressure) * S.RestVolume / (S.AirSpecificGasConstant * Temperature);
}
