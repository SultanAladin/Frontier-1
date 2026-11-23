import {RecentreGridWindow} from './SpatialGrid.js';
// Ported from unassignedinbox/Slate@3c987f33c71c2477bd73ca29c69d5bd0ee484d1a
//============================================================================================================================================
//                                                              PLUMESPACE.JS
//============================================================================================================================================
// 🧩 The smoke-and-fire cell lattices on the device: toroidal windows that follow the car, one fixed-rate step, a half-rate march.

// 📝 Frontier's tyre smoke (VolumetricDynamics/SmokeSequence.h + SmokeTransport.slang) solves a 32³ velocity field on
//    the CPU at 60 Hz with 12 cold-started Jacobi sweeps, uploads it every tick, advects a dense display volume over
//    every one of its cells on the device, relocates both lattices by a full copy when the car leaves the guard
//    band, and marches every display pixel. The same physics here costs far less:
//
//    | Concern            | Frontier (C++)                               | This exhibit                                     |
//    |--------------------|----------------------------------------------|--------------------------------------------------|
//    | velocity solve     | CPU, 32³, uploaded every tick                | device, never read back or uploaded              |
//    | pressure           | 12 Jacobi sweeps from zero each tick         | warm-started sweeps on the toroidal lattice      |
//    | moving window      | full-lattice copy on relocation (both sides) | toroidal relabelling, zero copies                |
//    | obstruction        | separate occupancy intake + chassis box      | one sample of the scene distance field, moving   |
//    |                    |                                              | occupants contribute their point velocity        |
//    | idle cost          | lattice resident until faint                 | no pass recorded at all once the plume has faded |
//    | march              | every display pixel, shadow march per sample | trace resolution, coverage-clipped, TAA-resolved |
//    | quality            | fixed at build                               | four grades, rebuilt live without a reload       |
//
//    One step runs at most once per frame at no more than 60 Hz (a 120 Hz display steps every other frame), its
//    duration the time accumulated since the last step, clamped to 1/30 s — semi-Lagrangian transport is
//    unconditionally stable, so a slow frame is one longer step rather than a spiral of short ones.

import { SourceStride, SourceCapacity } from "./PlumeSourceProjection.js";

const RecordBytes      = 256;          // [B]   PlumeRecord: sixteen vec4
const FireLightBytes   = 144;          // [B]   FireLightRecord
const FireLightLimit   = 4;            // [-]
const StepSeconds      = 1.0 / 60.0;   // [s]   shortest step
const StepCeiling      = 1.0 / 30.0;   // [s]   longest step
const FadeCeiling      = 12.0;         // [s]   SmokeSpecification::FadeCeiling — the plume is gone this long after the last source
const RestartOrigin    = 1 << 24;      // [cell] a prior window nothing lies in: every read of the old state returns nothing
const DetailExtent     = 32;           // [texel] tiling detail-noise volume edge
const FloorHeight      = -0.5;         // [m]   the window's floor, under the course
const FlameScale       = 80.0;         // [-]   flame emission at PlumeFire 1
const BytesPerFineCell = 16;           // [B]   two rgba16float thermo states
const BytesPerCoarseCell = 16 + 8 + 16 + 4 + 8;   // [B] two velocity states, light, curl, divergence, two pressures

// 📝 The smoke-and-fire quality slider — independent of the GI grade. Extent [fine cells, each even], CellExtent [m],
//    PressureSweeps (even), MarchSamples. A wider window keeps a long burnout's cloud resident; a finer cell resolves
//    the curls.
export const PlumeGrades = Object.freeze([
    { Name: "Low",    Extent: [64, 64, 20],   CellExtent: 0.40, PressureSweeps: 8,  MarchSamples: 32 },
    { Name: "Medium", Extent: [96, 96, 24],   CellExtent: 0.30, PressureSweeps: 12, MarchSamples: 40 },
    { Name: "High",   Extent: [128, 128, 32], CellExtent: 0.25, PressureSweeps: 16, MarchSamples: 48 },
    { Name: "Ultra",  Extent: [160, 160, 48], CellExtent: 0.20, PressureSweeps: 20, MarchSamples: 64 }
]);

// 📝 Every transport and optics reading the Smoke & fire editor exposes, at the values the exhibit opens with.
//    CoarseGasSettings and the tyre_burnout preset seeded them; the soot pair, the octaves and the detail are new.
export const PlumeDefaults = Object.freeze({
    PlumeEnabled: true, PlumeGrade: 2,
    PlumeEmissionRate: 1.0, PlumeDensity: 1.2, PlumeHeat: 1.0, PlumeBuoyancy: 2.4,
    PlumeExtinction: 3.6, PlumeAlbedo: 0.88, PlumeFire: 1.0, PlumeSoot: 1.0, PlumeSootAlbedo: 0.05,
    PlumeVorticity: 3.0, PlumeTurbulence: 0.6, PlumeTurbulenceScale: 3.7,
    PlumeDissipation: 0.10, PlumeCooling: 0.55, PlumeFlameDecay: 4.0,
    PlumeWindSpeed: 0.8, PlumeWindDirection: 30.0,
    PlumeScattering: 0.75, PlumeAnisotropy: 0.38, PlumeAmbient: 1.0, PlumeFireLight: 1.0,
    PlumeDetail: 0.7, PlumeDetailScale: 2.2, PlumeFlameCore: 1800.0, PlumeFlameTip: 1150.0, PlumeEmbers: 1.0
});

export class PlumeSpace
{
    // 📝 Configuration: one PlumeGrades entry.
    constructor(Exchange, Shaders, Configuration)
    {
        this.Exchange        = Exchange;
        this.Shaders         = Shaders;
        this.Resident        = false;
        this.StepPending     = false;
        this.State           = 0;
        this.Accumulator     = 0.0;
        this.QuietSeconds    = 0.0;
        this.StepOrdinal     = 0;
        this.ElapsedSeconds  = 0.0;
        this.StepsTaken      = 0;
        this.RecordBytes     = new ArrayBuffer(RecordBytes);
        this.FireLightBytes  = new ArrayBuffer(FireLightBytes);
        this.FireLightCount  = 0;
        this.CoverageReset   = new Int32Array([0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0, -0x7FFFFFFF, -0x7FFFFFFF, -0x7FFFFFFF, 0]);
        this.AssignConfiguration(Configuration);
    }

    AssignConfiguration(Configuration)
    {
        this.Configuration  = Configuration;
        this.ThermoExtent   = Configuration.Extent.slice();
        this.VelocityExtent = Configuration.Extent.map((Cells) => Cells / 2);
        this.CoarseEdge     = 2.0 * Configuration.CellExtent;
        this.Origin         = [0, 0];
        this.PriorOrigin    = [RestartOrigin, RestartOrigin];
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   RESOURCES
    //--------------------------------------------------------------------------------------------------------------------

    async ConstructResources(DistanceLayout, OccupantRecords)
    {
        this.OccupantRecords = OccupantRecords;
        const Storage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST;
        this.UniformRecords   = this.Exchange.ConstructRecords(RecordBytes, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Plume record");
        this.FireLightRecords = this.Exchange.ConstructRecords(FireLightBytes, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Fire lights");
        this.SourceRecords    = this.Exchange.ConstructRecords(SourceCapacity * SourceStride * 4, Storage, "Plume sources");
        this.CoverageRecords  = this.Exchange.ConstructRecords(32, Storage, "Plume coverage");
        this.Exchange.Device.queue.writeBuffer(this.CoverageRecords, 0, this.CoverageReset);
        this.Exchange.Device.queue.writeBuffer(this.FireLightRecords, 0, this.FireLightBytes);
        this.LatticeSampler   = this.Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear",
                                                                 addressModeU: "repeat", addressModeV: "repeat", addressModeW: "clamp-to-edge" });
        this.DetailSampler    = this.Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear",
                                                                 addressModeU: "repeat", addressModeV: "repeat", addressModeW: "repeat" });
        this.ConstructDetail();

        const Compute = GPUShaderStage.COMPUTE;
        this.AdvectionLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture3D", "Texture3D", "Sampler", "StorageTexture3D:rgba16float", "StorageRead", "Storage", "StorageRead"], Compute, "Plume advection");
        this.VorticityLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture3D", "Storage", "StorageTexture3D:rgba16float"], Compute, "Plume vorticity");
        this.PressureLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture3D", "Storage", "Storage", "Storage", "StorageTexture3D:rgba16float"], Compute, "Plume pressure");
        this.MarchLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Uniform", "Texture3D", "Sampler", "TextureDepth2D", "StorageRead", "TextureDepth2DLayered", "SamplerComparison", "Uniform",
             "Texture2D", "Sampler", "StorageTexture2D:rgba16float", "Texture3D", "Uniform", "Texture3D", "Sampler"], Compute, "Plume march");

        const Program = (File, Entry, Layouts) => this.Shaders.ConstructComputeProgram(File, Entry, Layouts);
        [this.AdvectVelocityProgram, this.AdvectThermoProgram, this.LightProgram, this.CurlProgram, this.ConfineProgram,
         this.DivergenceProgram, this.RelaxProgram, this.ProjectProgram, this.MarchProgram] = await Promise.all([
            Program("PlumeAdvection.wgsl", "AdvectVelocity", [this.AdvectionLayout, DistanceLayout]),
            Program("PlumeAdvection.wgsl", "AdvectThermo", [this.AdvectionLayout, DistanceLayout]),
            Program("PlumeAdvection.wgsl", "ResolveSunTransmittance", [this.AdvectionLayout, DistanceLayout]),
            Program("PlumeVorticity.wgsl", "ResolveCurl", [this.VorticityLayout]),
            Program("PlumeVorticity.wgsl", "ConfineVorticity", [this.VorticityLayout]),
            Program("PlumePressure.wgsl", "ResolveDivergence", [this.PressureLayout]),
            Program("PlumePressure.wgsl", "RelaxPressure", [this.PressureLayout]),
            Program("PlumePressure.wgsl", "ProjectVelocity", [this.PressureLayout]),
            Program("PlumeMarch.wgsl", "MarchPlume", [this.MarchLayout])
        ]);
        this.ConstructLattices();
    }

    // 📝 (Re)builds every lattice-sized resource and descriptor group for the current configuration. The programs do
    //    not depend on the extents — the record carries them — so a grade change costs allocations, never compiles.
    ConstructLattices()
    {
        for (const Texture of [...(this.VelocityTextures || []), ...(this.ThermoTextures || []), this.LightTexture])
            if (Texture) Texture.destroy();
        for (const Records of [this.CurlRecords, this.DivergenceRecords, ...(this.PressureRecords || [])])
            if (Records) Records.destroy();
        const Lattice = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
        const Construct = (Label, Extent) => this.Exchange.ConstructTexture({ label: Label, size: Extent, dimension: "3d", format: "rgba16float", usage: Lattice });
        this.VelocityTextures = [Construct("Plume velocity A", this.VelocityExtent), Construct("Plume velocity B", this.VelocityExtent)];
        this.ThermoTextures   = [Construct("Plume thermo A", this.ThermoExtent), Construct("Plume thermo B", this.ThermoExtent)];
        this.LightTexture     = Construct("Plume transmittance", this.VelocityExtent);
        this.LightView        = this.LightTexture.createView({ dimension: "3d" });
        this.VelocityViews    = this.VelocityTextures.map((Texture) => Texture.createView({ dimension: "3d" }));
        this.ThermoViews      = this.ThermoTextures.map((Texture) => Texture.createView({ dimension: "3d" }));
        const CoarseCells     = this.VelocityExtent[0] * this.VelocityExtent[1] * this.VelocityExtent[2];
        const Storage         = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST;
        this.CurlRecords       = this.Exchange.ConstructRecords(CoarseCells * 16, Storage, "Plume curl");
        this.DivergenceRecords = this.Exchange.ConstructRecords(CoarseCells * 4, Storage, "Plume divergence");
        this.PressureRecords   = [this.Exchange.ConstructRecords(CoarseCells * 4, Storage, "Plume pressure A"),
                                  this.Exchange.ConstructRecords(CoarseCells * 4, Storage, "Plume pressure B")];

        // 📝 Per state parity p: the step reads velocity[p] and thermo[p] and leaves the next state in [1 − p].
        const Group = (Layout, Resources) => this.Exchange.ConstructDescriptorGroup(Layout, Resources);
        const V = this.VelocityViews, T = this.ThermoViews, P = this.PressureRecords, Occupants = this.OccupantRecords;
        this.AdvectVelocityGroups = [0, 1].map((State) => Group(this.AdvectionLayout,
            [this.UniformRecords, V[State], T[State], this.LatticeSampler, V[1 - State], this.SourceRecords, this.CoverageRecords, Occupants]));
        this.AdvectThermoGroups = [0, 1].map((State) => Group(this.AdvectionLayout,
            [this.UniformRecords, V[1 - State], T[State], this.LatticeSampler, T[1 - State], this.SourceRecords, this.CoverageRecords, Occupants]));
        this.LightGroups = [0, 1].map((State) => Group(this.AdvectionLayout,
            [this.UniformRecords, V[1 - State], T[1 - State], this.LatticeSampler, this.LightView, this.SourceRecords, this.CoverageRecords, Occupants]));
        this.VorticityGroups = [0, 1].map((State) => Group(this.VorticityLayout,
            [this.UniformRecords, V[1 - State], this.CurlRecords, V[State]]));
        this.PressureGroups = [0, 1].map((State) => [
            Group(this.PressureLayout, [this.UniformRecords, V[State], this.DivergenceRecords, P[0], P[1], V[1 - State]]),
            Group(this.PressureLayout, [this.UniformRecords, V[State], this.DivergenceRecords, P[1], P[0], V[1 - State]])]);
        this.State = 0;
        this.Clear();
    }

    // 📝 Switches to another grade: new lattices, a clean plume. The caller rebuilds the march targets afterwards.
    ReconstructGrade(Configuration)
    {
        this.AssignConfiguration(Configuration);
        this.ConstructLattices();
    }

    // 📐 The tiling detail volume: three octaves of periodic value noise and one of inverted Worley cells, so the march
    //    can fold them into billows. Periodic in every axis, so a repeat sampler never shows a seam.
    ConstructDetail()
    {
        const Edge = DetailExtent;
        const Hash = (X, Y, Z, Period, Seed) =>
        {
            let Word = ((((X % Period) + Period) % Period) * 73856093) ^ ((((Y % Period) + Period) % Period) * 19349663)
                     ^ ((((Z % Period) + Period) % Period) * 83492791) ^ (Seed * 2654435761);
            Word = Math.imul(Word ^ (Word >>> 15), 0x2C1B3C6D);
            Word = Math.imul(Word ^ (Word >>> 12), 0x297A2D39);
            return ((Word ^ (Word >>> 15)) >>> 0) / 4294967296.0;
        };
        const Smooth = (Fraction) => Fraction * Fraction * (3.0 - 2.0 * Fraction);
        const Value = (X, Y, Z, Period, Seed) =>
        {
            const Scale = Period / Edge;
            const U = X * Scale, V = Y * Scale, W = Z * Scale;
            const I = Math.floor(U), J = Math.floor(V), K = Math.floor(W);
            const A = Smooth(U - I), B = Smooth(V - J), C = Smooth(W - K);
            let Sum = 0.0;
            for (let Corner = 0; Corner < 8; ++Corner)
            {
                const Dx = Corner & 1, Dy = (Corner >> 1) & 1, Dz = (Corner >> 2) & 1;
                Sum += Hash(I + Dx, J + Dy, K + Dz, Period, Seed) * (Dx ? A : 1 - A) * (Dy ? B : 1 - B) * (Dz ? C : 1 - C);
            }
            return Sum;
        };
        const Worley = (X, Y, Z, Period, Seed) =>
        {
            const Scale = Period / Edge;
            const U = X * Scale, V = Y * Scale, W = Z * Scale;
            const I = Math.floor(U), J = Math.floor(V), K = Math.floor(W);
            let Nearest = 9.0;
            for (let Dz = -1; Dz <= 1; ++Dz)
                for (let Dy = -1; Dy <= 1; ++Dy)
                    for (let Dx = -1; Dx <= 1; ++Dx)
                    {
                        const Cx = I + Dx, Cy = J + Dy, Cz = K + Dz;
                        const Px = Cx + Hash(Cx, Cy, Cz, Period, Seed), Py = Cy + Hash(Cx, Cy, Cz, Period, Seed + 1), Pz = Cz + Hash(Cx, Cy, Cz, Period, Seed + 2);
                        Nearest = Math.min(Nearest, (Px - U) ** 2 + (Py - V) ** 2 + (Pz - W) ** 2);
                    }
            return Math.max(0.0, 1.0 - Math.sqrt(Nearest));
        };
        const Texels = new Uint8Array(Edge * Edge * Edge * 4);
        for (let Z = 0; Z < Edge; ++Z)
            for (let Y = 0; Y < Edge; ++Y)
                for (let X = 0; X < Edge; ++X)
                {
                    const Offset = ((Z * Edge + Y) * Edge + X) * 4;
                    const Billow = Worley(X, Y, Z, 4, 11);
                    Texels[Offset]     = Math.round(255 * (0.55 * Value(X, Y, Z, 4, 1) + 0.45 * Billow));
                    Texels[Offset + 1] = Math.round(255 * (0.6 * Value(X, Y, Z, 8, 2) + 0.4 * Worley(X, Y, Z, 8, 21)));
                    Texels[Offset + 2] = Math.round(255 * Value(X, Y, Z, 16, 3));
                    Texels[Offset + 3] = Math.round(255 * Value(X, Y, Z, 8, 4));
                }
        this.DetailTexture = this.Exchange.ConstructTexture({ label: "Plume detail", size: [Edge, Edge, Edge], dimension: "3d", format: "rgba8unorm",
                                                              usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
        this.Exchange.Device.queue.writeTexture({ texture: this.DetailTexture }, Texels, { bytesPerRow: Edge * 4, rowsPerImage: Edge }, [Edge, Edge, Edge]);
        this.DetailView = this.DetailTexture.createView({ dimension: "3d" });
    }

    ConstructTargets(TraceExtent, FrameRecords, Surface, Shadow, Atmosphere)
    {
        if (this.MarchTexture)
            this.MarchTexture.destroy();
        this.TraceExtent = TraceExtent;
        this.MarchTexture = this.Exchange.ConstructTexture({ label: "Plume radiance", size: TraceExtent, format: "rgba16float",
                                                             usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING });
        this.MarchView = this.MarchTexture.createView();
        this.MarchGroups = [0, 1].map((State) => this.Exchange.ConstructDescriptorGroup(this.MarchLayout,
            [FrameRecords, this.UniformRecords, this.ThermoViews[State], this.LatticeSampler, Surface.Views.Depth, this.CoverageRecords,
             Shadow.LayeredView, Shadow.ComparisonSampler, Shadow.ReceiverRecords, Atmosphere.SkyView, Atmosphere.SkySampler, this.MarchView,
             this.LightView, this.FireLightRecords, this.DetailView, this.DetailSampler]));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    SCHEDULE
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Called once per frame with the sources of this frame. Decides residency and whether a step runs, moves the
    //    window when the focus leaves its middle half, and writes the step record, the sources and the coverage reset.
    Schedule(Seconds, FrameSeconds, Focus, Sources, Settings, SunDirection)
    {
        this.SunDirection = SunDirection || [0.0, 0.0, 1.0];
        this.StepPending = false;
        const Active = Settings.PlumeEnabled && Sources.Count > 0;
        if (Active)
        {
            this.QuietSeconds = 0.0;
            if (!this.Resident)
            {
                this.Resident = true;
                this.PriorOrigin = [RestartOrigin, RestartOrigin];
                this.Accumulator = StepSeconds;
                this.QuietPrior = 0.0;
                this.RecentreWindow(Focus, true);
            }
        }
        else if (this.Resident)
        {
            this.QuietSeconds += Seconds;
            if (this.QuietSeconds >= FadeCeiling || !Settings.PlumeEnabled)
                this.Resident = false;
        }
        if (!this.Resident)
        {
            this.Accumulator = 0.0;
            return;
        }
        this.Accumulator += Seconds;
        this.ElapsedSeconds += Seconds;
        if (this.Accumulator < StepSeconds)
            return;
        const Δτ = Math.min(this.Accumulator, StepCeiling);
        this.Accumulator = 0.0;
        this.RecentreWindow(Focus, false);
        this.EncodeRecord(Δτ, FrameSeconds, Sources, Settings);
        const Device = this.Exchange.Device;
        Device.queue.writeBuffer(this.UniformRecords, 0, this.RecordBytes);
        if (Sources.Count > 0)
            Device.queue.writeBuffer(this.SourceRecords, 0, Sources.Records, 0, Sources.Count * SourceStride);
        Device.queue.writeBuffer(this.CoverageRecords, 0, this.CoverageReset);
        this.StepPending = true;
    }

    RecentreWindow(Focus, Force)
    {
        this.Origin = RecentreGridWindow(Focus, this.CoarseEdge, this.VelocityExtent, this.Origin, Force);
    }

    EncodeRecord(Δτ, FrameSeconds, Sources, Settings)
    {
        const Integers = new Int32Array(this.RecordBytes);
        const Unsigned = new Uint32Array(this.RecordBytes);
        const Scalars  = new Float32Array(this.RecordBytes);
        const Read = (Key) => (Settings[Key] ?? PlumeDefaults[Key]);
        const Edge = this.Configuration.CellExtent;
        // 📝 Frontier's continuous fade once the sources stop: whatever remains reaches zero at FadeCeiling.
        let Fade = 1.0;
        if (this.QuietSeconds > 0.0)
        {
            const Remaining = Math.max(0.0, FadeCeiling - this.QuietSeconds);
            Fade = Remaining / Math.max(Δτ, FadeCeiling - (this.QuietPrior || 0.0));
        }
        this.QuietPrior = this.QuietSeconds;
        const Dissipation = Math.min(0.95, Read("PlumeDissipation"));
        const WindAngle = Read("PlumeWindDirection") * Math.PI / 180.0;
        const Wind = [Math.cos(WindAngle) * Read("PlumeWindSpeed"), Math.sin(WindAngle) * Read("PlumeWindSpeed"), 0.0];
        Integers.set([this.Origin[0], this.Origin[1], 0, this.StepOrdinal++], 0);
        Integers.set([this.PriorOrigin[0], this.PriorOrigin[1], 0, Sources.Count], 4);
        Unsigned.set([...this.VelocityExtent, 0], 8);
        Unsigned.set([...this.ThermoExtent, Settings.Inspection === 10 ? 1 : 0], 12);
        Scalars.set([Edge, 2.0 * Edge, FloorHeight, Δτ], 16);
        Scalars.set([Read("PlumeBuoyancy"), 0.12, Read("PlumeVorticity"), Read("PlumeTurbulence")], 20);
        Scalars.set([Math.pow(1.0 - Dissipation, Δτ) * Fade, Math.pow(1.0 - Math.min(0.95, Read("PlumeCooling")), Δτ) * Fade,
                     Math.exp(-Read("PlumeFlameDecay") * Δτ) * Fade, Math.pow(1.0 - 0.08, Δτ)], 24);
        Scalars.set([...Wind, this.ElapsedSeconds], 28);
        const Bounds = Sources.ResolveBounds();
        Scalars.set([...Bounds.Minimum.map((Component) => Number.isFinite(Component) ? Component : 0.0), Math.max(FrameSeconds, 1.0e-3)], 32);
        Scalars.set([...Bounds.Maximum.map((Component) => Number.isFinite(Component) ? Component : 0.0), FlameScale * Read("PlumeFire")], 36);
        Scalars.set([Read("PlumeExtinction"), Read("PlumeAlbedo"), 3.0 * Read("PlumeExtinction"), this.Configuration.MarchSamples], 40);
        Scalars.set([...this.SunDirection, Read("PlumeSootAlbedo")], 44);
        Scalars.set([2.4 * Read("PlumeSoot"), 25.0 * Read("PlumeHeat"), Math.pow(1.0 - 0.8 * Dissipation, Δτ) * Fade, 0.0], 48);
        Scalars.set([Read("PlumeScattering"), Read("PlumeAnisotropy"), Read("PlumeAmbient"), Read("PlumeFireLight")], 52);
        Scalars.set([Read("PlumeDetail"), Read("PlumeDetailScale"), Read("PlumeTurbulenceScale"), 0.0], 56);
        Scalars.set([Read("PlumeFlameCore"), Read("PlumeFlameTip"), 30.0, 0.0], 60);
        this.PriorOrigin = this.Origin.slice();
    }

    // 📝 Lights: [{ Position, Radius, Radiance: [r, g, b] }], brightest first; written every frame, resident or not,
    //    because the direct radiance pass lights the scene with them.
    AssignFireLights(Lights)
    {
        const Unsigned = new Uint32Array(this.FireLightBytes);
        const Scalars = new Float32Array(this.FireLightBytes);
        Scalars.fill(0.0);
        const Count = Math.min(FireLightLimit, Lights.length);
        Unsigned[0] = Count;
        for (let Index = 0; Index < Count; ++Index)
        {
            const Light = Lights[Index];
            Scalars.set([...Light.Position, Light.Radius], 4 + Index * 4);
            Scalars.set([...Light.Radiance, 0.0], 20 + Index * 4);
        }
        this.FireLightCount = Count;
        this.Exchange.Device.queue.writeBuffer(this.FireLightRecords, 0, this.FireLightBytes);
    }

    // 📝 Forgets the plume at once: the next source restarts a clean lattice.
    Clear()
    {
        this.Resident = false;
        this.StepPending = false;
        this.QuietSeconds = 0.0;
        this.PriorOrigin = [RestartOrigin, RestartOrigin];
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   RECORDING
    //--------------------------------------------------------------------------------------------------------------------

    RecordTransport(Encoder, DistanceGroup, TimestampWrites)
    {
        if (!this.StepPending)
            return;
        const State = this.State;
        const Coarse = this.VelocityExtent.map((Cells) => Math.ceil(Cells / 4));
        const Fine = this.ThermoExtent.map((Cells) => Math.ceil(Cells / 4));
        const Pass = Encoder.beginComputePass({ label: "Plume transport", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.AdvectVelocityProgram);
        Pass.setBindGroup(0, this.AdvectVelocityGroups[State]);
        Pass.setBindGroup(1, DistanceGroup);
        Pass.dispatchWorkgroups(...Coarse);
        Pass.setPipeline(this.CurlProgram);
        Pass.setBindGroup(0, this.VorticityGroups[State]);
        Pass.dispatchWorkgroups(...Coarse);
        Pass.setPipeline(this.ConfineProgram);
        Pass.dispatchWorkgroups(...Coarse);
        Pass.setPipeline(this.DivergenceProgram);
        Pass.setBindGroup(0, this.PressureGroups[State][0]);
        Pass.dispatchWorkgroups(...Coarse);
        Pass.setPipeline(this.RelaxProgram);
        for (let Sweep = 0; Sweep < this.Configuration.PressureSweeps; ++Sweep)
        {
            Pass.setBindGroup(0, this.PressureGroups[State][Sweep & 1]);
            Pass.dispatchWorkgroups(...Coarse);
        }
        Pass.setPipeline(this.ProjectProgram);
        Pass.setBindGroup(0, this.PressureGroups[State][0]);
        Pass.dispatchWorkgroups(...Coarse);
        Pass.setPipeline(this.AdvectThermoProgram);
        Pass.setBindGroup(0, this.AdvectThermoGroups[State]);
        Pass.setBindGroup(1, DistanceGroup);
        Pass.dispatchWorkgroups(...Fine);
        Pass.setPipeline(this.LightProgram);
        Pass.setBindGroup(0, this.LightGroups[State]);
        Pass.dispatchWorkgroups(...Coarse);
        Pass.end();
        this.State = 1 - State;
        this.StepPending = false;
        this.StepsTaken++;
    }

    RecordMarch(Encoder, TimestampWrites)
    {
        if (!this.Resident || !this.MarchGroups)
            return;
        const Pass = Encoder.beginComputePass({ label: "Plume march", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.MarchProgram);
        Pass.setBindGroup(0, this.MarchGroups[this.State]);
        Pass.dispatchWorkgroups(Math.ceil(this.TraceExtent[0] / 8), Math.ceil(this.TraceExtent[1] / 8));
        Pass.end();
    }

    QueryDescription()
    {
        const [X, Y, Z] = this.ThermoExtent, [CX, CY, CZ] = this.VelocityExtent;
        const Edge = this.Configuration.CellExtent;
        const Bytes = X * Y * Z * BytesPerFineCell + CX * CY * CZ * BytesPerCoarseCell;
        return { Thermo: `${X}×${Y}×${Z} @ ${(Edge * 100).toFixed(0)} cm`, Velocity: `${CX}×${CY}×${CZ} @ ${(2 * Edge * 100).toFixed(0)} cm`,
                 Span: `${(X * Edge).toFixed(0)} × ${(Y * Edge).toFixed(0)} × ${(Z * Edge).toFixed(0)} m`, Cells: X * Y * Z + CX * CY * CZ,
                 Bytes, Grade: this.Configuration.Name, Sweeps: this.Configuration.PressureSweeps, Samples: this.Configuration.MarchSamples };
    }
}
