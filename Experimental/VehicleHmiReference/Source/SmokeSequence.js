// Adapter for the actual Slate 3c987f3 tyre plume system, not the former smoke renderer.
import { PlumeSpace, PlumeGrades, PlumeDefaults } from './PlumeSpace.js';
import { PlumeSourceProjection } from './PlumeSourceProjection.js';
import { RotateVector, MultiplyQuaternion, ConstructAxisQuaternion } from './TransformProjection.js';
const PressureSweepsDefault = 16;
export const GasParameterCatalogue = Object.freeze({
    // source — multipliers on the tyre model's own rates, which stay the physical port
    Emission:         { Default: 1.0,  Minimum: 0.0,  Maximum: 4.0,  Step: 0.05, Unit: "×",      Label: "Emission rate" },
    Heat:             { Default: 1.0,  Minimum: 0.0,  Maximum: 4.0,  Step: 0.05, Unit: "×",      Label: "Temperature" },
    Expansion:        { Default: 1.0,  Minimum: 0.0,  Maximum: 3.0,  Step: 0.05, Unit: "×",      Label: "Volume expansion" },
    Soot:             { Default: 1.0,  Minimum: 0.0,  Maximum: 4.0,  Step: 0.05, Unit: "×",      Label: "Soot yield" },
    Momentum:         { Default: 9.0,  Minimum: 0.0,  Maximum: 24.0, Step: 0.5,  Unit: "1/s",    Label: "Carrier drag" },
    TreadEjection:    { Default: 1.0,  Minimum: 0.0,  Maximum: 3.0,  Step: 0.05, Unit: "×",      Label: "Tread ejection" },
    // simulation
    Buoyancy:         { Default: 2.4,  Minimum: 0.0,  Maximum: 8.0,  Step: 0.1,  Unit: "m/s²·K", Label: "Buoyancy" },
    Dissipation:      { Default: 0.10, Minimum: 0.0,  Maximum: 0.9,  Step: 0.01, Unit: "1/s",    Label: "Dissipation" },
    Cooling:          { Default: 0.55, Minimum: 0.0,  Maximum: 0.95, Step: 0.01, Unit: "1/s",    Label: "Cooling" },
    Vorticity:        { Default: 3.0,  Minimum: 0.0,  Maximum: 6.0,  Step: 0.1,  Unit: "ε",      Label: "Vorticity confinement" },
    Turbulence:       { Default: 0.60, Minimum: 0.0,  Maximum: 2.0,  Step: 0.02, Unit: "m/s",    Label: "Turbulence" },
    TurbulenceScale:  { Default: 3.7,  Minimum: 0.2,  Maximum: 4.0,  Step: 0.05, Unit: "1/m",    Label: "Turbulence frequency" },
    WindSpeed:        { Default: 0.8,  Minimum: 0.0,  Maximum: 12.0, Step: 0.1,  Unit: "m/s",    Label: "Wind speed" },
    WindHeading:      { Default: 30.0, Minimum: 0.0,  Maximum: 360.0, Step: 1.0, Unit: "°",      Label: "Wind heading" },
    TimeScale:        { Default: 1.0,  Minimum: 0.25, Maximum: 2.0,  Step: 0.05, Unit: "×",      Label: "Time scale" },
    PressureSweeps:   { Default: PressureSweepsDefault, Minimum: 4, Maximum: 40, Step: 2, Unit: "",  Label: "Pressure iterations" },
    // appearance
    Density:          { Default: 1.0,  Minimum: 0.2,  Maximum: 3.0,  Step: 0.05, Unit: "×",      Label: "Density" },
    Albedo:           { Default: 0.88, Minimum: 0.2,  Maximum: 0.98, Step: 0.01, Unit: "",       Label: "Albedo" },
    Phase:            { Default: 0.38, Minimum: 0.0,  Maximum: 0.9,  Step: 0.01, Unit: "g",      Label: "Forward scattering" },
    MultiScatter:     { Default: 0.75, Minimum: 0.0,  Maximum: 1.5,  Step: 0.05, Unit: "",       Label: "Multiple scattering" },
    Ambient:          { Default: 1.0,  Minimum: 0.0,  Maximum: 3.0,  Step: 0.05, Unit: "×",      Label: "Ambient light" },
    Detail:           { Default: 0.70, Minimum: 0.0,  Maximum: 1.0,  Step: 0.02, Unit: "",       Label: "Detail" },
    DetailScale:      { Default: 2.2,  Minimum: 0.2,  Maximum: 4.0,  Step: 0.05, Unit: "1/m",    Label: "Detail frequency" },
    Erosion:          { Default: 1.0,  Minimum: 0.0,  Maximum: 1.5,  Step: 0.05, Unit: "",       Label: "Edge erosion" },
    Warp:             { Default: 0.0,  Minimum: 0.0,  Maximum: 1.0,  Step: 0.05, Unit: "",       Label: "Billow warp" },
    Powder:           { Default: 0.0,  Minimum: 0.0,  Maximum: 1.0,  Step: 0.05, Unit: "",       Label: "Powder" },
    // fire
    FlameIntensity:   { Default: 1.0,  Minimum: 0.0,  Maximum: 4.0,  Step: 0.05, Unit: "×",      Label: "Flame intensity" },
    FlameTemperature: { Default: 1.0,  Minimum: 0.7,  Maximum: 1.6,  Step: 0.01, Unit: "×",      Label: "Flame temperature" },
    FlameCore:        { Default: 1800, Minimum: 1300, Maximum: 2400, Step: 10,   Unit: "K",      Label: "Flame core" },
    FlameTip:         { Default: 1150, Minimum: 900,  Maximum: 1600, Step: 10,   Unit: "K",      Label: "Flame tip" },
    FlameFlicker:     { Default: 0.6,  Minimum: 0.0,  Maximum: 1.0,  Step: 0.02, Unit: "",       Label: "Flicker" },
    FlameLoss:        { Default: 0.96, Minimum: 0.5,  Maximum: 0.995, Step: 0.005, Unit: "1/s",  Label: "Flame decay" },
    FireLight:        { Default: 1.0,  Minimum: 0.0,  Maximum: 4.0,  Step: 0.05, Unit: "×",      Label: "Fire light" }
});
export function ConstructGasParameters() {
    return Object.fromEntries(Object.entries(GasParameterCatalogue).map(([Key, Value]) => [Key, Value.Default]));
}
export function MapPlumeSettings(Gas, Enabled = true) {
    return { ...PlumeDefaults, PlumeEnabled: Enabled,
        PlumeEmissionRate: Gas.Emission, PlumeDensity: 1.2 * Gas.Density, PlumeHeat: Gas.Heat,
        PlumeSoot: Gas.Soot, PlumeBuoyancy: Gas.Buoyancy, PlumeDissipation: Gas.Dissipation,
        PlumeCooling: Gas.Cooling, PlumeVorticity: Gas.Vorticity, PlumeTurbulence: Gas.Turbulence,
        PlumeTurbulenceScale: Gas.TurbulenceScale, PlumeWindSpeed: Gas.WindSpeed, PlumeWindDirection: Gas.WindHeading,
        PlumeAlbedo: Gas.Albedo, PlumeAnisotropy: Gas.Phase, PlumeScattering: Gas.MultiScatter,
        PlumeAmbient: Gas.Ambient, PlumeDetail: Gas.Detail, PlumeDetailScale: Gas.DetailScale,
        PlumeFire: Gas.FlameIntensity, PlumeFlameCore: Gas.FlameCore * Gas.FlameTemperature,
        PlumeFlameTip: Gas.FlameTip * Gas.FlameTemperature, PlumeFireLight: Gas.FireLight,
        PlumeFlameDecay: -Math.log(Math.max(0.005, 1 - Gas.FlameLoss)) };
}
export function ProjectPlumeTelemetry(Vehicle, Readings, Thermal) {
    return { Position: Vehicle.Position, Velocity: Vehicle.LinearVelocity, Exhausts: [], AfterfireSeconds: 0,
        Wheels: Readings.map((Reading, Index) => {
            const Wheel = Vehicle.Wheels[Index], Heat = Thermal.Wheels[Index];
            const Heading = RotateVector(MultiplyQuaternion(Vehicle.Orientation, ConstructAxisQuaternion([0,0,1], Wheel.Steer)), [1,0,0]);
            return { ...Reading, InContact: Reading.ContactFraction > 0.5 && !Reading.OnDebris,
                // Bare rims have no tyre rubber to vaporise.
                SlidingPower: Reading.Shredded ? 0 : Reading.SlidingPower,
                Thermal: { Ignited: !Reading.Shredded && Heat.Ignited, CombustionPower: Heat.CombustionPower,
                    SurfaceTemperature: Heat.Surface }, Burst: !!Heat.Burst,
                Radius: Wheel.Radius, Longitudinal: Heading, SurfaceSpeed: Wheel.Spin * Wheel.Radius };
        }) };
}
export class SmokeSequence extends PlumeSpace {
    constructor(Exchange, Shaders, GradeName) {
        const Grade = PlumeGrades[{ low: 0, medium: 1, high: 2 }[GradeName] ?? 2];
        super(Exchange, Shaders, { ...Grade, Extent: [...Grade.Extent] });
        this.Sources = new PlumeSourceProjection();
        this.Extent = this.ThermoExtent;
        this.FireLight = { Position: [0,0,0], Intensity: 0, Colour: [1,0.46,0.14] };
        this.Metrics = { Sources: 0, Ticks: 0, Relocations: 0 };
    }
    async ConstructPrograms(FrameRecords, DistanceLayout, Atmosphere, OccupantRecords, Shadow) {
        this.FrameRecords = FrameRecords; this.Atmosphere = Atmosphere; this.Shadow = Shadow;
        await this.ConstructResources(DistanceLayout, OccupantRecords);
    }
    ConstructScreenTargets(ScreenExtent, Surface, Display, MarchScale, TraceExtent) {
        this.ConstructTargets(TraceExtent, this.FrameRecords, Surface, this.Shadow, this.Atmosphere);
        Display.AssignSmoke(this.MarchView);
    }
    Advance(Seconds, { Enabled, Vehicle, Light, Gas, Readings, Thermal, Emissions = [] }) {
        const Settings = MapPlumeSettings(Gas, Enabled);
        const Step = Math.min(0.1, Seconds * Gas.TimeScale);
        this.Sources.Project(ProjectPlumeTelemetry(Vehicle, Readings, Thermal), Step,
            { EmissionRate: Settings.PlumeEmissionRate, SmokeYield: Settings.PlumeDensity,
                HeatYield: Settings.PlumeHeat, SootYield: Settings.PlumeSoot, FireLight: Settings.PlumeFireLight });
        // Preserve editor/fracture/burst plumes without duplicating the four tyre sources.
        for (const E of Emissions.slice(4)) {
            if (!E.Active || this.Sources.Count >= 24) continue;
            const Offset = this.Sources.Count++ * 12;
            this.Sources.Records.set([...E.Position, E.Radius || 0.3, ...(E.Velocity || [0,0,1]), 0,
                E.SmokeRate || 0, E.HeatRate || 0, E.FlameRate || 0, E.SootRate || 0], Offset);
        }
        const Prior = this.Origin?.join(',');
        this.Configuration.PressureSweeps = Math.max(4, 2 * Math.round(Gas.PressureSweeps / 2));
        this.Schedule(Step, Seconds, Vehicle.Position, this.Sources, Settings, Light.Direction);
        this.AssignFireLights(Enabled ? this.Sources.Lights : []);
        this.Metrics.Sources = this.Sources.Count;
        this.Metrics.Ticks = this.StepsTaken;
        if (Prior !== this.Origin?.join(',')) this.Metrics.Relocations++;
        const Lamps = Enabled ? this.Sources.Lights : [], Total = Lamps.reduce((Sum, L) => Sum + L.Radiance[0], 0);
        this.FireLight.Intensity = Total;
        if (Total > 0) this.FireLight.Position = [0,1,2].map(A => Lamps.reduce((Sum,L) => Sum + L.Position[A] * L.Radiance[0], 0) / Total);
    }
    Clear() { super.Clear(); this.Sources?.Reset(); if (this.FireLight) this.FireLight.Intensity = 0; }
    QueryReadout() {
        const D = this.QueryDescription();
        return [`Slate 3c987f3 plume · ${D.Grade} · ${D.Thermo}`, `sources ${this.Sources.Count} · steps ${this.StepsTaken} · ${this.Resident ? 'resident' : 'idle'}`];
    }
}
