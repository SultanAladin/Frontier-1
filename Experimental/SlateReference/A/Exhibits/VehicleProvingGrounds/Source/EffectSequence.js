//============================================================================================================================================
//                                                            EFFECTSEQUENCE.JS
//============================================================================================================================================
// 🧩 Derez and rez — the car (and any cone) coming apart into glowing cubes and flying back together, after the Particle Editor's presets.

// 📝 The Particle Editor's "Derez cube", "Reverse derez" and "Glitch dissolve" presets, brought into the renderer:
//
//      • the surface raster masks the car cell by cell on one release clock (EffectRecords.wgsl) — glowing white-hot,
//        then cyan, then gone;
//      • the effect raster draws a cube from every anchor of a released cell, its flight a closed-form function of
//        the clock (EffectRaster.wgsl), into the same surface projection — so the cubes are lit, glow, reflect and
//        bounce light in the GI like any emissive surface;
//      • nothing is simulated or read back: an emitter is 128 bytes of uniform and one instanced draw while it lasts.
//
//    The car's anchors are 1 920 points spread over its own facets by area (body and wheels), uploaded once. A cone
//    (or any box) uses a 6 × 6 grid on each face of its bounds. Eight emitters run at once.

import { VertexStride } from "./FacetStructure.js";
import { CourseMaterial, DerezPalette } from "./MaterialSpecification.js";
import { ConstructRigidTransform, LengthVector } from "./TransformProjection.js";

const EmitterSlots   = 8;
const EmitterStride  = 128;               // [B]
const RecordHeader   = 96;                // [B] EffectRecord up to Emitters (EffectRecords.wgsl)
const PoseCapacity   = 360;               // [-] 6 s of the car's motion at 60 Hz
const PoseSpacing    = 1.0 / 60.0;        // [s] EffectPoseSpacing
const PoseFloats     = 16;                // [-] EffectPose
const RecordExtent   = RecordHeader + EmitterSlots * EmitterStride;
const VehicleBodyCells  = 1536;
const VehicleWheelCells = 96;             // per wheel
export const VehicleCellCount = VehicleBodyCells + 4 * VehicleWheelCells;
const BoxCellCount   = 216;               // 6 × 6 per face

export const EffectKind = Object.freeze({ VehicleDerez: 0, VehicleRez: 1, BoxDerez: 2, BoxRez: 3, Debris: 4 });
export const MaskMode   = Object.freeze({ Off: 0, Derez: 1, Rez: 2, Hidden: 3 });

// 📝 The three Particle Editor presets, in this renderer's terms.
// 📝 The Particle Editor's timings (a 0.6 s hold and a 1.6 s wave for one showcase cube) are scaled to a respawn's pace —
//    about a second and a half each way — keeping each preset's character; the sliders reach the originals.
export const DerezPresets = Object.freeze({
    "Derez cube":      { Style: 0, HoldOffset: 0.3, Wave: 1.0, Burst: 0.5, SpeedMinimum: 0.6, SpeedMaximum: 2.6, Drag: 0.4, Life: 3.5,
                         Size: 0.075, Flight: 1.2, Glow: "#35d6ff", Hot: "#ffffff", Glitch: "#ff2d6f" },
    "Reverse derez":   { Style: 0, HoldOffset: 0.15, Wave: 0.8, Burst: 0.3, SpeedMinimum: 0.4, SpeedMaximum: 1.8, Drag: 0.6, Life: 2.8,
                         Size: 0.07, Flight: 1.3, Glow: "#35d6ff", Hot: "#ffffff", Glitch: "#ff2d6f" },
    "Glitch dissolve": { Style: 1, HoldOffset: 0.1, Wave: 0.9, Burst: 0.2, SpeedMinimum: 0.2, SpeedMaximum: 1.2, Drag: 1.2, Life: 2.0,
                         Size: 0.06, Flight: 1.0, Glow: "#00ffd0", Hot: "#ffffff", Glitch: "#ff2d6f" }
});

// 📝 Faces: the cubes are the car's own facets — their colours, glowing in them — rather than the palette.
//    Momentum: the car carries on moving while it comes apart, and every cube leaves with the car's velocity at its
//    release. GlowStrength: the face glow, as a multiple of each facet's albedo.
export const EffectSettingsDefaults = Object.freeze({ Preset: "Derez cube", ...DerezPresets["Derez cube"], Intensity: 1.0,
                                                      Faces: true, Momentum: true, GlowStrength: 6.0 });

export class EffectSequence
{
    constructor(Exchange, Shaders, Scene)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.Scene    = Scene;
        this.Settings = { ...EffectSettingsDefaults };
        this.Clock    = 0.0;
        this.FrameSeconds = 1.0 / 60.0;
        this.Emitters = new Array(EmitterSlots).fill(null);
        this.Mask     = { Mode: MaskMode.Off, Start: 0.0, Seed: 1 };
        this.Bounds   = { Minimum: [-2.5, -1.0, -0.6], Maximum: [2.5, 1.0, 0.9] };
        this.Records  = Exchange.ConstructRecords(RecordExtent, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Effect record");
        this.Metrics  = { Emitted: 0, Live: 0 };
        // 📝 The car's motion through a derez, sampled at 60 Hz on the effect clock (EffectPose).
        this.Poses     = new Float32Array(PoseCapacity * PoseFloats);
        this.PoseCount = 0;
        this.PoseStart = 0.0;
        this.PoseFlushed = 0;
        this.Recording = null;
        this.PoseRecords = Exchange.ConstructRecords(PoseCapacity * PoseFloats * 4, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, "Derez poses");
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  BRING-UP
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Anchors spread over the car's facets by area (deterministic), in the car-body frame; wheels at rest pose.
    ConstructVehicleCells(Vehicle)
    {
        const Scene = this.Scene;
        const Cells = new Float32Array(VehicleCellCount * 8);
        const Body = Scene.Occupants[Scene.VehicleOccupants.Body];
        const BodySet = Scene.FacetSets[Body.FacetSet];
        this.Bounds = { Minimum: BodySet.LocalMinimum.slice(), Maximum: BodySet.LocalMaximum.slice() };
        let Cursor = 0;
        const Random = ConstructStream(0xC0FFEE);
        const Spread = (Facets, Count, Transform) =>
        {
            const Areas = [];
            let Total = 0.0;
            for (const Facet of Facets)
            {
                const [A, B, C] = Facet.Corners;
                const U = [B[0] - A[0], B[1] - A[1], B[2] - A[2]], V = [C[0] - A[0], C[1] - A[1], C[2] - A[2]];
                const Cross = [U[1] * V[2] - U[2] * V[1], U[2] * V[0] - U[0] * V[2], U[0] * V[1] - U[1] * V[0]];
                const Area = 0.5 * LengthVector(Cross);
                Total += Area;
                Areas.push({ Total, Normal: Area > 0.0 ? Cross.map((Component) => Component / (2.0 * Area)) : [0.0, 0.0, 1.0] });
            }
            for (let Sample = 0; Sample < Count; ++Sample)
            {
                // 📝 Stratified along the cumulative area, so the anchors cover the car evenly.
                const Target = (Sample + Random()) / Count * Total;
                let Low = 0, High = Areas.length - 1;
                while (Low < High)
                {
                    const Middle = (Low + High) >> 1;
                    if (Areas[Middle].Total < Target) Low = Middle + 1; else High = Middle;
                }
                const [A, B, C] = Facets[Low].Corners;
                let R1 = Random(), R2 = Random();
                if (R1 + R2 > 1.0) { R1 = 1.0 - R1; R2 = 1.0 - R2; }
                let Point = [0, 1, 2].map((Axis) => A[Axis] + R1 * (B[Axis] - A[Axis]) + R2 * (C[Axis] - A[Axis]));
                let Normal = Areas[Low].Normal;
                if (Transform)
                {
                    Point = TransformPoint(Transform, Point);
                    Normal = TransformDirection(Transform, Normal);
                }
                // 📝 w: the facet's family — the colour the cube wears and glows in.
                Cells.set([...Point, Facets[Low].Family, ...Normal, 0.0], Cursor * 8);
                Cursor++;
            }
        };
        // 📝 The faces seen from outside: the cabin furniture stays out of the spread (the door mirrors are coat).
        Spread(BodySet.Facets.filter((Facet) => Facet.Family < CourseMaterial.CabinLeather || Facet.Family === CourseMaterial.BodyCoat),
               VehicleBodyCells, null);
        for (let Wheel = 0; Wheel < 4; ++Wheel)
        {
            const Occupant = Scene.Occupants[Scene.VehicleOccupants.Wheels[Wheel]];
            const Mount = Vehicle.Wheels[Wheel].Mount;
            const Hub = [Mount[0], Mount[1], Mount[2] - Vehicle.StaticLength];
            Spread(Scene.FacetSets[Occupant.FacetSet].Facets, VehicleWheelCells, ConstructRigidTransform(Hub, [0.0, 0.0, 0.0, 1.0]));
        }
        for (let Cell = 0; Cell < VehicleCellCount; ++Cell)
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                this.Bounds.Minimum[Axis] = Math.min(this.Bounds.Minimum[Axis], Cells[Cell * 8 + Axis]);
                this.Bounds.Maximum[Axis] = Math.max(this.Bounds.Maximum[Axis], Cells[Cell * 8 + Axis]);
            }
        this.VehicleCells = this.Exchange.ConstructRecordsFrom(Cells, GPUBufferUsage.STORAGE, "Vehicle derez cells");
    }

    async ConstructPrograms(FrameRecords)
    {
        this.Layout = this.Exchange.ConstructDescriptorLayout(["Uniform", "Uniform", "StorageRead", "StorageRead"],
                                                              GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, "Effect raster");
        this.Group = this.Exchange.ConstructDescriptorGroup(this.Layout, [FrameRecords, this.Records, this.VehicleCells, this.PoseRecords]);
        this.Program = await this.Shaders.ConstructRasterProgram("EffectRaster.wgsl", {
            VertexEntry: "EffectVertex", FragmentEntry: "EffectFragment",
            VertexLayouts: [],
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "greater" },
            Targets: [{ format: "rgba16float" }, { format: "rg16float" }]
        }, [this.Layout]);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  EMITTERS
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Seconds an emitter of this kind lives: the latest release or arrival plus a cube's flight and its children.
    QueryDuration(Kind)
    {
        const S = this.Settings;
        const Scale = Kind === EffectKind.VehicleDerez || Kind === EffectKind.VehicleRez ? 1.0 : 0.45;
        if (Kind === EffectKind.Debris)
            return 4.0;
        if (Kind === EffectKind.VehicleRez || Kind === EffectKind.BoxRez)
            return Scale * (0.3 + S.Flight * 1.15) + 0.05;
        return Scale * (S.HoldOffset + S.Wave * (S.Style === 1 ? 2.0 : 1.15)) + S.Life * 1.15 + 1.2;
    }

    // 📝 Seconds until every cell of the car has let go (derez) or is back (rez) — when the mask is complete.
    QueryMaskDuration(Mode)
    {
        const S = this.Settings;
        if (Mode === MaskMode.Rez)
            return 0.3 + S.Flight * 1.15 + 0.4;
        return S.HoldOffset + S.Wave * (S.Style === 1 ? 2.0 : 1.15) + 0.1;
    }

    Emit(Kind, Transform, Velocity, Angular, Centre, Extent, Count)
    {
        let Slot = this.Emitters.findIndex((Emitter) => !Emitter);
        if (Slot < 0)
        {
            // 📝 All busy: the one closest to finishing gives way.
            Slot = 0;
            for (let Index = 1; Index < EmitterSlots; ++Index)
                if (this.Emitters[Index].End < this.Emitters[Slot].End)
                    Slot = Index;
        }
        const Seed = (Math.random() * 0x7FFFFFFF) | 0;
        this.Emitters[Slot] = { Kind, Transform: Transform.slice(), Velocity: Velocity.slice(), Angular: Angular.slice(), Centre: Centre.slice(),
                                Extent: Extent.slice(), Count, Seed, Start: this.Clock, End: this.Clock + this.QueryDuration(Kind) };
        this.Metrics.Emitted++;
        return this.Emitters[Slot];
    }

    // 📝 The car dissolves: mask on, one emitter carrying the car's velocity and spin into its cubes. With Momentum the
    //    car keeps moving (RecoveryDirector leaves it to the solver, coasting) and its pose is recorded from now on, so
    //    each cube leaves from where the car is at its cell's release, with the car's velocity there.
    DerezVehicle(Vehicle)
    {
        const Emitter = this.Emit(EffectKind.VehicleDerez, Vehicle.QueryBodyTransform(), Vehicle.LinearVelocity, Vehicle.AngularVelocity,
                                  [0.0, 0.0, 0.0], [1.0, 1.0, 1.0], VehicleCellCount);
        this.Mask = { Mode: MaskMode.Derez, Start: this.Clock, Seed: Emitter.Seed, Emitter };
        this.PoseCount = 0;
        this.PoseFlushed = 0;
        this.PoseStart = this.Clock;
        this.Recording = this.Settings.Momentum ? { Vehicle, Prior: CapturePose(Vehicle, this.Clock) } : null;
        if (this.Recording)
            this.WritePose(this.Recording.Prior);
        return this.QueryMaskDuration(MaskMode.Derez);
    }

    WritePose(Pose)
    {
        if (this.PoseCount >= PoseCapacity)
            return;
        this.Poses.set([...Pose.Position, Pose.Clock, ...Pose.Rotation, ...Pose.Velocity, 0.0, ...Pose.Angular, 0.0], this.PoseCount * PoseFloats);
        this.PoseCount++;
    }

    // 📝 After the vehicle steps: every 60 Hz sample time since the last frame, interpolated between the frames' poses.
    RecordPoses()
    {
        const Recording = this.Recording;
        if (!Recording)
            return;
        const Current = CapturePose(Recording.Vehicle, this.Clock);
        const Prior = Recording.Prior;
        while (this.PoseCount < PoseCapacity)
        {
            const Clock = this.PoseStart + this.PoseCount * PoseSpacing;
            if (Clock > this.Clock + 1.0e-6)
                break;
            const Fraction = Math.min(1.0, Math.max(0.0, (Clock - Prior.Clock) / Math.max(Current.Clock - Prior.Clock, 1.0e-6)));
            this.WritePose(InterpolatePose(Prior, Current, Fraction, Clock));
        }
        Recording.Prior = Current;
        if (this.PoseCount >= PoseCapacity)
            this.Recording = null;
    }

    // 📝 Ends the recording (the car is hidden, then moved): the samples stay for the cubes still in flight.
    StopPoses()
    {
        this.Recording = null;
    }

    RezVehicle(Vehicle)
    {
        const Body = this.Bounds;
        const Centre = [0, 1, 2].map((Axis) => 0.5 * (Body.Minimum[Axis] + Body.Maximum[Axis]));
        const Emitter = this.Emit(EffectKind.VehicleRez, Vehicle.QueryBodyTransform(), [0.0, 0.0, 0.0], [0.0, 0.0, 0.0], Centre, [1.0, 1.0, 1.0],
                                  VehicleCellCount);
        this.Mask = { Mode: MaskMode.Rez, Start: this.Clock, Seed: Emitter.Seed, Emitter };
        return this.QueryMaskDuration(MaskMode.Rez);
    }

    HideVehicle()
    {
        this.Mask = { ...this.Mask, Mode: MaskMode.Hidden };
    }

    ClearVehicleMask()
    {
        this.Mask = { ...this.Mask, Mode: MaskMode.Off };
    }

    // 📝 A box-shaped thing (a cone) bursting into cubes, or assembling from them; Transform is its local → world.
    //    Family is what the cubes wear with face colours (a cone's orange picks its band and base by height).
    DerezBox(Transform, Centre, Extent, Velocity = [0.0, 0.0, 0.0], Family = CourseMaterial.ConeOrange)
    {
        const Emitter = this.Emit(EffectKind.BoxDerez, Transform, Velocity, [0.0, 0.0, 0.0], Centre, Extent, BoxCellCount);
        Emitter.Transform[3] = Family;
        return Emitter;
    }

    RezBox(Transform, Centre, Extent, Family = CourseMaterial.ConeOrange)
    {
        const Emitter = this.Emit(EffectKind.BoxRez, Transform, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0], Centre, Extent, BoxCellCount);
        Emitter.Transform[3] = Family;
        return this.QueryDuration(EffectKind.BoxRez);
    }

    // 📝 A tyre letting go: rubber chunks thrown out of the wheel's box at once, with the car's velocity (no glow).
    BurstTyre(Transform, Extent, Velocity, Family = CourseMaterial.Tyre)
    {
        const Emitter = this.Emit(EffectKind.Debris, Transform, Velocity, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0], Extent, BoxCellCount);
        Emitter.Transform[3] = Family;
        return Emitter;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  PALETTE
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 The glow colours are material records (families DerezGlow / DerezHot / DerezGlitch): rewritten in place.
    ApplyPalette(MaterialRecords)
    {
        const S = this.Settings;
        const Write = (Family, Hex, Luminance, Albedo) =>
        {
            const Linear = ParseColour(Hex);
            const Record = new Float32Array([...Albedo, 0.3, ...Linear.map((Channel) => Channel * Luminance * S.Intensity), 0.0, 0.0, 0.0, 0.04, 0.0]);
            this.Exchange.Device.queue.writeBuffer(MaterialRecords, Family * 48, Record);
        };
        Write(CourseMaterial.DerezGlow, S.Glow, 5.0, [0.05, 0.10, 0.12]);
        Write(CourseMaterial.DerezHot, S.Hot, 10.0, [0.2, 0.2, 0.2]);
        Write(CourseMaterial.DerezGlitch, S.Glitch, 5.0, [0.10, 0.03, 0.05]);
    }

    ApplyPreset(Name, MaterialRecords)
    {
        if (!DerezPresets[Name])
            return;
        Object.assign(this.Settings, DerezPresets[Name], { Preset: Name });
        if (MaterialRecords)
            this.ApplyPalette(MaterialRecords);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   FRAME
    //--------------------------------------------------------------------------------------------------------------------

    Advance(Seconds)
    {
        this.Clock += Seconds;
        this.FrameSeconds = Math.max(Seconds, 1.0e-4);
        this.RecordPoses();
        if (this.PoseCount > this.PoseFlushed)
        {
            const First = this.PoseFlushed * PoseFloats, Last = this.PoseCount * PoseFloats;
            this.Exchange.Device.queue.writeBuffer(this.PoseRecords, First * 4, this.Poses, First, Last - First);
            this.PoseFlushed = this.PoseCount;
        }
        let Live = 0;
        for (let Slot = 0; Slot < EmitterSlots; ++Slot)
        {
            const Emitter = this.Emitters[Slot];
            if (!Emitter)
                continue;
            if (this.Clock > Emitter.End && Emitter !== this.Mask.Emitter)
                this.Emitters[Slot] = null;
            else if (this.Clock > Emitter.End)
                this.Emitters[Slot] = null;
            else
                Live++;
        }
        this.Metrics.Live = Live;
        this.Upload();
    }

    Upload()
    {
        const S = this.Settings;
        const Bytes = new ArrayBuffer(RecordExtent);
        const Scalars = new Float32Array(Bytes), Integers = new Uint32Array(Bytes);
        Scalars[0] = this.Clock;
        Integers[1] = this.Mask.Mode;
        Scalars[2] = this.Mask.Start;
        Integers[3] = S.Style;
        Scalars.set(this.Bounds.Minimum, 4);
        Scalars[7] = S.HoldOffset;
        Scalars.set(this.Bounds.Maximum, 8);
        Scalars[11] = this.FrameSeconds;
        Scalars.set([S.Burst, S.SpeedMinimum, S.SpeedMaximum, S.Drag], 12);
        Scalars.set([S.Life, S.Size, S.Wave, S.Flight], 16);
        Scalars.set([S.Faces ? 1.0 : 0.0, S.GlowStrength * S.Intensity, this.PoseCount >= 2 ? this.PoseCount : 0, this.PoseStart], 20);
        // 📝 Slot 0 also carries the mask's seed (the surface raster reads Emitters[0].Centre.w), so the mask's emitter
        //    always goes into slot 0 of the record — the others follow in any order.
        const Order = this.Emitters.map((Emitter, Slot) => Slot);
        const MaskSlot = this.Emitters.indexOf(this.Mask.Emitter);
        if (MaskSlot > 0)
            [Order[0], Order[MaskSlot]] = [Order[MaskSlot], Order[0]];
        this.DrawOrder = Order;
        Order.forEach((Slot, Index) =>
        {
            const Emitter = this.Emitters[Slot];
            const Offset = (RecordHeader + Index * EmitterStride) / 4;
            if (!Emitter)
            {
                Scalars[Offset + 16 + 7] = 255;   // Angular.w: no kind
                Scalars[Offset + 16 + 11] = 0;    // Extent.w: no particles
                Scalars[Offset + 16 + 15] = Index === 0 ? this.Mask.Seed : 0;
                return;
            }
            Scalars.set(Emitter.Transform, Offset);
            Scalars.set([...Emitter.Velocity, Emitter.Start], Offset + 16);
            Scalars.set([...Emitter.Angular, Emitter.Kind], Offset + 20);
            Scalars.set([...Emitter.Extent, Emitter.Count], Offset + 24);
            Scalars.set([...Emitter.Centre, Index === 0 && !this.Mask.Emitter ? this.Mask.Seed : Emitter.Seed], Offset + 28);
        });
        this.Exchange.Device.queue.writeBuffer(this.Records, 0, Bytes);
    }

    // 📝 Inside the surface projection pass: one instanced draw per live emitter (36 corners × parent and 8 children).
    Record(Pass)
    {
        if (!this.Program || !this.DrawOrder)
            return;
        let Bound = false;
        this.DrawOrder.forEach((Slot, Index) =>
        {
            const Emitter = this.Emitters[Slot];
            if (!Emitter)
                return;
            if (!Bound)
            {
                Pass.setPipeline(this.Program);
                Pass.setBindGroup(0, this.Group);
                Bound = true;
            }
            Pass.draw(36 * 9, Emitter.Count, 0, Index * 4096);
        });
    }

    QueryReadout()
    {
        const Mode = ["off", "derez", "rez", "hidden"][this.Mask.Mode];
        return [`emitters live: ${this.Metrics.Live} of ${EmitterSlots} · emitted ${this.Metrics.Emitted} · car mask ${Mode}`,
                `car anchors ${VehicleCellCount} · cone anchors ${BoxCellCount} · style ${this.Settings.Style === 1 ? "glitch" : "cubes"}`
                + ` · ${this.Settings.Faces ? "face colours" : "palette"}`,
                `momentum ${this.Settings.Momentum ? "on" : "off"} · poses ${this.PoseCount}${this.Recording ? " (recording)" : ""}`];
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HELPERS
//------------------------------------------------------------------------------------------------------------------------

// 📐 #rrggbb (sRGB) → linear Rec.709.
export function ParseColour(Hex)
{
    const Value = parseInt(String(Hex).replace("#", ""), 16) || 0;
    return [(Value >> 16) & 255, (Value >> 8) & 255, Value & 255].map((Byte) =>
    {
        const Channel = Byte / 255.0;
        return Channel <= 0.04045 ? Channel / 12.92 : Math.pow((Channel + 0.055) / 1.055, 2.4);
    });
}

function TransformPoint(Transform, Point)
{
    return [Transform[0] * Point[0] + Transform[4] * Point[1] + Transform[8] * Point[2] + Transform[12],
            Transform[1] * Point[0] + Transform[5] * Point[1] + Transform[9] * Point[2] + Transform[13],
            Transform[2] * Point[0] + Transform[6] * Point[1] + Transform[10] * Point[2] + Transform[14]];
}

function TransformDirection(Transform, Direction)
{
    return [Transform[0] * Direction[0] + Transform[4] * Direction[1] + Transform[8] * Direction[2],
            Transform[1] * Direction[0] + Transform[5] * Direction[1] + Transform[9] * Direction[2],
            Transform[2] * Direction[0] + Transform[6] * Direction[1] + Transform[10] * Direction[2]];
}

// 📝 The car's pose and velocity now, on the effect clock.
function CapturePose(Vehicle, Clock)
{
    return { Clock, Position: Vehicle.Position.slice(), Rotation: Vehicle.Orientation.slice(), Velocity: Vehicle.LinearVelocity.slice(),
             Angular: Vehicle.AngularVelocity.slice() };
}

function InterpolatePose(A, B, Fraction, Clock)
{
    const Lerp = (U, V) => U.map((Component, Index) => Component + (V[Index] - Component) * Fraction);
    const Dot = A.Rotation.reduce((Sum, Component, Index) => Sum + Component * B.Rotation[Index], 0.0);
    const Target = Dot < 0.0 ? B.Rotation.map((Component) => -Component) : B.Rotation;
    const Rotation = Lerp(A.Rotation, Target);
    const Length = Math.hypot(...Rotation) || 1.0;
    return { Clock, Position: Lerp(A.Position, B.Position), Rotation: Rotation.map((Component) => Component / Length),
             Velocity: Lerp(A.Velocity, B.Velocity), Angular: Lerp(A.Angular, B.Angular) };
}

function ConstructStream(Seed)
{
    let Accumulator = (Seed >>> 0) || 1;
    return () =>
    {
        Accumulator = (Math.imul(1664525, Accumulator) + 1013904223) >>> 0;
        return Accumulator / 4294967296;
    };
}

export { DerezPalette, VertexStride };
