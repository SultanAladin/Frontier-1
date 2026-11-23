//========================================================================================================================
//                                                  DeviceHost.js                                                  🧩
//========================================================================================================================
//
// 📝 WebGPU provisioning, shader assembly, the erosion sim and the render loop.
//
//    The contract this prototype is built around, now THREE speeds rather than two:
//
//      TOPOLOGY EDIT  -> retranscribe + createShaderModule + both pipelines      ~tens of ms
//      DIAL TURN      -> writeBuffer into the uniform + RESEED the grid          ~one dispatch
//      SIM STEP       -> one compute dispatch per process, no recompile          per frame
//
//    🔴 Pipeline creation is NOT free and must never land in the render loop. Pipelines are cached on the
//       topology stamp; a dial turn that triggered a recompile would drop frames on every slider drag,
//       which is the single easiest way to make a node editor feel broken.
//
//    🔴 A dial turn RESTARTS the sim rather than adjusting it in place. Erosion has history: there is no
//       way to retroactively apply a changed hardness to material that has already been removed. Reseeding
//       is one dispatch, so restarting is cheap — pretending the change applies retroactively is not.

import { DistanceExpressionPreamble } from "./DistanceExpression.js";
import { GridMarchResolve }           from "../Simulation/GridMarch.js";

// 🔴 The analytic march, kept for the CARD PREVIEWS only. The viewport marches the voxel grid; a preview
//    cannot, because the grid holds the eroded root and an upstream entry has no grid of its own.
import { SphereTraceResolve }         from "./SphereTrace.js";
import { ConstructionTranscriber }    from "../Construction/ConstructionTranscriber.js";
import { ConstructionSpecificationTable, ComposeWeatherSequence }
    from "../Construction/ConstructionSpecifications.js";
import { ComposeTopologyStamp, SlotCeiling } from "../Construction/TreeState.js";

import { PreviewEdge, BakeEdge, ComposeVoxelField, DiscardVoxelField,
         ErosionBinding, MarchBinding, FlipParity, WriteFieldProfile,
         DispatchSpan }                      from "../Simulation/VoxelField.js";
import { ErosionProcessTable, ComposeErosionSource } from "../Simulation/ErosionCompute.js";
import { ComposeSeedSource, ComposeSeedLayout,
         ComposeSeedBindings }               from "../Simulation/FieldSeed.js";

// 📝 The head occupies 8 vec4 lanes. COUNT THE LANES, NOT THE SCALARS — a vec3f is 16-byte aligned, so
//    each vec3f claims a whole lane and its trailing f32 fills the gap. The head is 19 scalars, but those
//    do NOT pack into ceil(19/4) = 5 lanes:
//
//      0 Origin.xyz       + StepScale        4 SolarBearing.xyz + AmbientWeight
//      1 Forward.xyz      + StepCeiling      5 Viewport.xy, CavityWeight, SlopeWeight
//      2 Rightward.xyz    + HitTolerance     6 ResolveMode, ShadowWeight, Exposure, SceneRadius
//      3 Upward.xyz       + FarDistance      7 PreviewMode + 3 reserved
//
//    🔴 This was 5, derived from the scalar count, and it broke three things at once: the buffer was
//       848 B where the pipeline demanded 880 B, entry slot 0 was written over head lanes 5-6 (clobbering
//       Viewport/Cavity/Slope/ResolveMode/Shadow/Exposure), and every dial landed two lanes early.
//    ⚠️ Adding a head field means re-counting LANES here and in WriteViewProfile's Put calls.
const ViewProfileVectors = 8;                                       // [idx] - vec4 lanes in the head
const UniformVectorTally  = ViewProfileVectors + SlotCeiling;
const UniformByteLength   = UniformVectorTally * 16;

// 📝 The domain half-extent the voxel grid covers. The seed body is authored in world units, so this must
//    comfortably contain it — the seed seals the outer two cells to empty, and a body that reaches the
//    wall would be clipped flat there rather than weathered.
const DomainRadius = 2.6;                                           // [m]

export function ComposeDeviceHost(CanvasSurface, PresentNotice)
{
    return {
        Canvas       : CanvasSurface,
        Notice       : PresentNotice,

        // 📝 The full-resolution extent. Seeded from the markup and rewritten by the host whenever the
        //    element resizes — the viewport is half the window now, not a fixed square.
        //
        //    🔴 Held here rather than read back off the canvas: the backing store is driven BELOW this
        //       while interacting, so reading it would shrink the target on every preview cycle.
        FrameWidth   : CanvasSurface.width,
        FrameHeight  : CanvasSurface.height,

        Device       : null,
        Context      : null,
        Format       : null,
        Uniform      : null,
        Binding      : null,
        BindingLayout: null,
        Pipeline     : null,
        PipelineStamp: null,
        Scratch      : new Float32Array(UniformVectorTally * 4),
        Ready        : false,
        LastError    : null,
        LineTally    : 0,

        // ── simulation ──────────────────────────────────────────────────────────────────────────────
        Field        : null,                                        // the voxel grid (ping-pong pair)
        SeedLayout   : null,
        SeedBindings : null,
        SeedPipelines: null,                                        // { Mass, Resistance }
        ErodePipelines: null,                                       // Naming -> GPUComputePipeline
        Sequence     : [],                                          // ordered processes from the tree
        Baking       : false,
        SeedPending  : true                                         // reseed before the next step
    };
}

export async function ProvisionDevice(Host)
{
    if (!navigator.gpu)
    {
        Host.LastError = "WebGPU is unavailable — this build needs a browser exposing navigator.gpu";
        Host.Notice(Host.LastError, "bad");
        return false;
    }

    const Adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
    if (!Adapter)
    {
        Host.LastError = "no GPU adapter — a software fallback would not sustain the march";
        Host.Notice(Host.LastError, "bad");
        return false;
    }

    Host.Device = await Adapter.requestDevice();
    Host.Format = navigator.gpu.getPreferredCanvasFormat();
    Host.Context = Host.Canvas.getContext("webgpu");
    Host.Context.configure({
        device    : Host.Device,
        format    : Host.Format,
        alphaMode : "opaque"
    });

    // 🔴 Report device loss rather than letting the canvas quietly freeze on the last good frame.
    Host.Device.lost.then(Loss =>
    {
        Host.Ready = false;
        Host.LastError = `device lost — ${Loss.reason || "unknown"}`;
        Host.Notice(Host.LastError, "bad");
    });

    Host.Device.onuncapturederror = Report =>
    {
        Host.LastError = Report.error.message;
        Host.Notice(`GPU error — ${Report.error.message}`, "bad");
    };

    Host.Uniform = Host.Device.createBuffer({
        size  : UniformByteLength,
        usage : GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });

    // 📝 VERTEX visibility as well as FRAGMENT and COMPUTE: the seed's transcribed tree reads U.Entry from
    //    a compute stage, and the same layout backs both pipelines.
    Host.BindingLayout = Host.Device.createBindGroupLayout({
        entries:
        [{
            binding    : 0,
            visibility : GPUShaderStage.FRAGMENT | GPUShaderStage.COMPUTE,
            buffer     : { type: "uniform" }
        }]
    });

    Host.Binding = Host.Device.createBindGroup({
        layout  : Host.BindingLayout,
        entries : [{ binding: 0, resource: { buffer: Host.Uniform } }]
    });

    ProvisionField(Host, PreviewEdge);

    Host.Ready = true;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   VOXEL FIELD
//------------------------------------------------------------------------------------------------------------------------

// 📝 Allocate (or reallocate) the grid at an edge. Switching tiers destroys and rebuilds, because a
//    WebGPU buffer cannot be resized — and because the bind groups cache buffer references, they must be
//    rebuilt with it.
//
//    🔴 The seed bind groups are derived from the field, so they are rebuilt HERE. Keeping a stale seed
//       binding across a tier switch would write the 256³ seed into the 128³ buffers.
function ProvisionField(Host, Edge)
{
    DiscardVoxelField(Host.Field);

    Host.Field      = ComposeVoxelField(Host.Device, Edge, DomainRadius);
    Host.SeedLayout = Host.SeedLayout || ComposeSeedLayout(Host.Device);
    Host.SeedBindings = ComposeSeedBindings(Host.Device, Host.SeedLayout, Host.Field);
    Host.SeedPending  = true;

    // ⚠️ Pipelines bind LAYOUTS, not buffers, so they survive a reallocation at the same edge — but the
    //    march layout object itself is new, so the render pipeline must be rebuilt.
    Host.PipelineStamp = null;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 SHADER ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

// 🔴 This declaration is COMPILER-VERIFIED, not reasoned about. The resolve half makes 26 direct
//    `U.<field>` references and the transcriber emits `U.Entry<Slot>`, so every field must sit at the
//    top level of the bound struct. Embedding the preamble's ViewProfile as a named member instead
//    was tried and rejected by wgpu-native with "invalid field accessor `StepCeiling`" — the refs
//    would all have needed U.Head.StepCeiling. Flat is the form that compiles.
//
//    📝 ViewProfile in the preamble is therefore documentation of the layout, not the bound type.
//       Reserved closes the head on a 16-byte boundary so Entry[0] starts at a lane edge; without it
//       every dial would be read one float out of phase.
//
//    ⚠️ Field order here MUST match WriteViewProfile's packing below AND the preamble's ViewProfile.
//       Three places, one layout — change one, change all three.
const DialDeclaration = `
struct DialProfile
{
    Origin        : vec3f,
    StepScale     : f32,
    Forward       : vec3f,
    StepCeiling   : f32,
    Rightward     : vec3f,
    HitTolerance  : f32,
    Upward        : vec3f,
    FarDistance   : f32,
    SolarBearing  : vec3f,
    AmbientWeight : f32,
    Viewport      : vec2f,
    CavityWeight  : f32,
    SlopeWeight   : f32,
    ResolveMode   : f32,
    ShadowWeight  : f32,
    Exposure      : f32,
    SceneRadius   : f32,
    PreviewMode   : f32,
    Reserved      : f32,
    Reserved2     : f32,
    Reserved3     : f32,
    Entry         : array<vec4f, ${SlotCeiling}>
};
@group(0) @binding(0) var<uniform> U : DialProfile;
`;

// 📝 Assemble the render shader: fixed library + transcribed tree + the GRID march. The tree still sits
//    between the two, but what it feeds has changed — it supplies the tint expression and (for the seed
//    module) the starting body, while the surface itself now comes from the voxel grid.
export function ComposeShaderSource(TreeState)
{
    const Transcription = ConstructionTranscriber.Transcribe(TreeState);

    return {
        Source: DistanceExpressionPreamble + DialDeclaration + Transcription.Source + GridMarchResolve,
        LineTally: Transcription.LineTally
    };
}

export async function AssemblePipeline(Host, TreeState)
{
    if (!Host.Ready) return false;

    const Stamp = ComposeTopologyStamp(TreeState);
    if (Stamp === Host.PipelineStamp && Host.Pipeline) return true;      // ① dial-only change

    const Transcription = ConstructionTranscriber.Transcribe(TreeState);
    Host.LineTally = Transcription.LineTally;

    // ② Scope error capture so a compile failure is reported against THIS module, not the last one.
    Host.Device.pushErrorScope("validation");

    const RenderSource =
        DistanceExpressionPreamble + DialDeclaration + Transcription.Source + GridMarchResolve;
    const RenderModule = Host.Device.createShaderModule({ code: RenderSource });

    if (!await ReportCompilation(Host, RenderModule, RenderSource, "march"))
    {
        await Host.Device.popErrorScope();
        return false;
    }

    // ③ The two compute modules. Both reuse the same preamble and the same transcribed tree — the seed
    //    needs the tree for the starting body and the resistance field; erosion needs the preamble's
    //    noise helpers only, but sharing the module keeps them from drifting apart.
    const SeedSource = ComposeSeedSource(
        DistanceExpressionPreamble, DialDeclaration, Transcription.Source);
    const SeedModule = Host.Device.createShaderModule({ code: SeedSource });

    if (!await ReportCompilation(Host, SeedModule, SeedSource, "seed"))
    {
        await Host.Device.popErrorScope();
        return false;
    }

    const ErodeSource = ComposeErosionSource(DistanceExpressionPreamble, "");
    const ErodeModule = Host.Device.createShaderModule({ code: ErodeSource });

    if (!await ReportCompilation(Host, ErodeModule, ErodeSource, "erode"))
    {
        await Host.Device.popErrorScope();
        return false;
    }

    // ④ Render pipeline. Group 0 is the dials, group 1 is the field — the same split the seed uses.
    const Pipeline = Host.Device.createRenderPipeline({
        layout: Host.Device.createPipelineLayout({
            bindGroupLayouts: [Host.BindingLayout, Host.Field.Layouts.MarchLayout]
        }),
        vertex:
        {
            module     : RenderModule,
            entryPoint : "TranscribeQuadVertex"
        },
        fragment:
        {
            module     : RenderModule,
            entryPoint : "InscribeSurfaceFragment",
            targets    : [{ format: Host.Format }]
        },
        primitive: { topology: "triangle-list" }
    });

    // ⑤ Seed pipelines — two entry points over one module, both binding dials AND field.
    const SeedPipelineLayout = Host.Device.createPipelineLayout({
        bindGroupLayouts: [Host.BindingLayout, Host.SeedLayout]
    });

    const SeedPipelines =
    {
        Mass: Host.Device.createComputePipeline({
            layout  : SeedPipelineLayout,
            compute : { module: SeedModule, entryPoint: "DriveMassSeed" }
        }),
        Resistance: Host.Device.createComputePipeline({
            layout  : SeedPipelineLayout,
            compute : { module: SeedModule, entryPoint: "DriveResistanceSeed" }
        })
    };

    // ⑥ One erosion pipeline per process. These bind ONLY the field — erosion reads no dials, because
    //    every weather parameter travels in the FieldProfile instead.
    const ErodeLayout = Host.Device.createPipelineLayout({
        bindGroupLayouts: [Host.Field.Layouts.ErosionLayout]
    });

    const ErodePipelines = {};
    for (const Process of ErosionProcessTable)
    {
        ErodePipelines[Process.Naming] = Host.Device.createComputePipeline({
            layout  : ErodeLayout,
            compute : { module: ErodeModule, entryPoint: Process.Entry }
        });
    }

    const Validation = await Host.Device.popErrorScope();
    if (Validation)
    {
        Host.LastError = Validation.message;
        Host.Notice(`pipeline — ${Validation.message}`, "bad");
        return false;
    }

    Host.Pipeline       = Pipeline;
    Host.SeedPipelines  = SeedPipelines;
    Host.ErodePipelines = ErodePipelines;
    Host.PipelineStamp  = Stamp;
    Host.LastError      = null;

    // 🔴 A topology change rewrites the starting body, so the sim MUST restart. Continuing to erode a
    //    grid seeded from the previous tree would show the old rock wearing away under the new dials.
    Host.SeedPending = true;
    return true;
}

// 📝 Report WGSL diagnostics against a named module. Returns false on error, having archived the source.
async function ReportCompilation(Host, Module, Source, Naming)
{
    const Diagnostics = await Module.getCompilationInfo();
    const Failures = Diagnostics.messages.filter(Message => Message.type === "error");
    if (Failures.length === 0) return true;

    const First = Failures[0];
    Host.LastError = `WGSL ${Naming} ${First.lineNum}:${First.linePos} — ${First.message}`;
    Host.Notice(Host.LastError, "bad");
    ArchiveFailedSource(Host, Source, Failures, Naming);
    return false;
}

// 📝 Keep the failing source reachable from the console. A WGSL line number against generated code is
//    useless without the generated code in hand.
function ArchiveFailedSource(Host, Source, Failures, Naming)
{
    const Numbered = Source.split("\n")
        .map((Line, Index) => `${String(Index + 1).padStart(4, " ")}  ${Line}`)
        .join("\n");

    globalThis.RockFormationFailedShader = Numbered;
    console.error(`WGSL compile failed (${Naming}). Source in globalThis.RockFormationFailedShader`);
    for (const Failure of Failures)
    {
        console.error(`  ${Failure.lineNum}:${Failure.linePos} ${Failure.message}`);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SIMULATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Recompute which weather processes run, and in what order, from the tree. Called on every topology
//    change — the chain is authored by wiring Weather nodes in series, so the order is the tree's.
export function RefreshSequence(Host, TreeState)
{
    Host.Sequence = ComposeWeatherSequence(TreeState);
}

// 📝 Fill both halves of the ping-pong pair plus the resistance buffer.
//
//    🔴 BOTH halves. Seeding one leaves the other holding the previous run's density, and the first
//       erosion step reads the stale half — the rock appears to flicker between two shapes on alternate
//       steps, which reads as a race and is not one.
export function SeedField(Host, TreeState, Weather)
{
    if (!Host.Ready || !Host.SeedPipelines) return;

    const Field = Host.Field;
    Field.StepTally = 0;
    Field.Parity    = 0;

    WriteFieldProfile(Host.Device, Field, Weather);

    const Span = DispatchSpan(Field);
    const Encoder = Host.Device.createCommandEncoder({ label: "Seed" });
    const Sweep = Encoder.beginComputePass({ label: "Seed" });

    Sweep.setBindGroup(0, Host.Binding);

    for (let Half = 0; Half < 2; Half++)
    {
        Sweep.setPipeline(Host.SeedPipelines.Mass);
        Sweep.setBindGroup(1, Host.SeedBindings[Half]);
        Sweep.dispatchWorkgroups(Span[0], Span[1], Span[2]);
    }

    // ⚠️ Resistance is written once, not per half — it is a single buffer both seed bindings point at.
    //    Dispatching it twice would be harmless but wasteful; dispatching it zero times leaves the rock
    //    uniformly hard and every process degenerates into isotropic shrinking.
    Sweep.setPipeline(Host.SeedPipelines.Resistance);
    Sweep.setBindGroup(1, Host.SeedBindings[0]);
    Sweep.dispatchWorkgroups(Span[0], Span[1], Span[2]);

    Sweep.end();
    Host.Device.queue.submit([Encoder.finish()]);

    Host.SeedPending = false;
}

// 📝 Advance the sim by StepTally rounds. Each round runs every wired process in sequence, and each
//    process is a SEPARATE dispatch with a parity flip between — a process must read a consistent
//    snapshot, so chaining two inside one dispatch would have half the grid reading pre-step values.
export function DriveErosion(Host, Weather, Rounds)
{
    if (!Host.Ready || !Host.ErodePipelines || Host.Sequence.length === 0) return 0;

    const Field = Host.Field;
    const Span  = DispatchSpan(Field);
    const Encoder = Host.Device.createCommandEncoder({ label: "Erode" });

    let Ran = 0;

    for (let Round = 0; Round < Rounds; Round++)
    {
        for (const Step of Host.Sequence)
        {
            const Pipeline = Host.ErodePipelines[Step.Process];
            if (!Pipeline) continue;

            // 🔴 The profile carries StepIndex, which several processes use to decorrelate their noise.
            //    It must be rewritten per dispatch, before the pass that reads it — writeBuffer is ordered
            //    against the queue, so it cannot be hoisted out of the loop.
            WriteFieldProfile(Host.Device, Field, Weather);

            const Sweep = Encoder.beginComputePass({ label: Step.Process });
            Sweep.setPipeline(Pipeline);
            Sweep.setBindGroup(0, ErosionBinding(Field));
            Sweep.dispatchWorkgroups(Span[0], Span[1], Span[2]);
            Sweep.end();

            FlipParity(Field);                                       // AFTER the dispatch is encoded
            Ran++;
        }
    }

    Host.Device.queue.submit([Encoder.finish()]);
    return Ran;
}

// 📝 Switch tiers for a bake. Returns the edge actually provisioned.
//
//    ⚠️ The caller must reassemble the pipeline afterwards: ProvisionField clears the topology stamp
//       precisely so the next AssemblePipeline rebuilds against the new march layout.
export function ApplyBakeTier(Host, Wanted)
{
    const Edge = Wanted ? BakeEdge : PreviewEdge;
    if (Host.Field && Host.Field.Edge === Edge) return Edge;

    ProvisionField(Host, Edge);
    Host.Baking = Wanted;
    return Edge;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   UNIFORM WRITE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Pack the uniform. Lane order MUST match the ViewProfile field order in DistanceExpression.js —
//    the struct interleaves vec3f with a trailing f32 per lane precisely so there is no implicit padding
//    for the two sides to disagree about.
export function WriteViewProfile(Host, TreeState, ViewProfile)
{
    if (!Host.Ready) return;

    const S = Host.Scratch;
    let Lane = 0;
    const Put = (x, y, z, w) => { S[Lane++] = x; S[Lane++] = y; S[Lane++] = z; S[Lane++] = w; };

    Put(ViewProfile.Origin[0],    ViewProfile.Origin[1],    ViewProfile.Origin[2],    ViewProfile.StepScale);
    Put(ViewProfile.Forward[0],   ViewProfile.Forward[1],   ViewProfile.Forward[2],   ViewProfile.StepCeiling);
    Put(ViewProfile.Rightward[0], ViewProfile.Rightward[1], ViewProfile.Rightward[2], ViewProfile.HitTolerance);
    Put(ViewProfile.Upward[0],    ViewProfile.Upward[1],    ViewProfile.Upward[2],    ViewProfile.FarDistance);
    Put(ViewProfile.SolarBearing[0], ViewProfile.SolarBearing[1], ViewProfile.SolarBearing[2],
        ViewProfile.AmbientWeight);
    Put(ViewProfile.Viewport[0], ViewProfile.Viewport[1], ViewProfile.CavityWeight, ViewProfile.SlopeWeight);
    Put(ViewProfile.ResolveMode, ViewProfile.ShadowWeight, ViewProfile.Exposure, ViewProfile.SceneRadius);
    Put(ViewProfile.PreviewMode, 0.0, 0.0, 0.0);

    // ① Entry dials, one vec4 per slot, in dial order.
    for (const Entry of TreeState.Entries.values())
    {
        const Base = (ViewProfileVectors + Entry.Slot) * 4;
        if (Base + 3 >= S.length) continue;

        const Specification = ConstructionSpecificationTable[Entry.Species];
        for (let Index = 0; Index < 4; Index++)
        {
            S[Base + Index] = Index < Specification.Dials.length ? Entry.Dials[Index] : 0.0;
        }
    }

    Host.Device.queue.writeBuffer(Host.Uniform, 0, S.buffer, 0, UniformByteLength);
}

// 📝 Resize the canvas BACKING STORE while CSS holds the displayed size, so the browser upscales the
//    smaller render for free. Halving the divisor quarters the pixel count, and pixels — not voxels —
//    are what the RESOLVE spends its time on.
//
//    Returns true when the size actually changed, because the caller must re-write Viewport in the
//    uniform when it does. 🔴 Forgetting that skews the ray fan and the image shears.
export function ApplyResolveScale(Host, Divisor)
{
    const Frame = Host.Canvas;
    const Wide  = Math.max(64, Math.round(Host.FrameWidth  / Divisor));
    const Tall  = Math.max(64, Math.round(Host.FrameHeight / Divisor));

    if (Frame.width === Wide && Frame.height === Tall) return false;

    Frame.width  = Wide;
    Frame.height = Tall;
    return true;
}

export function InscribeSurface(Host)
{
    if (!Host.Ready || !Host.Pipeline) return;

    const Encoder = Host.Device.createCommandEncoder();
    const Attachment = Host.Context.getCurrentTexture().createView();

    const Resolve = Encoder.beginRenderPass({
        colorAttachments:
        [{
            view       : Attachment,
            clearValue : { r: 0.02, g: 0.02, b: 0.024, a: 1.0 },
            loadOp     : "clear",
            storeOp    : "store"
        }]
    });

    Resolve.setPipeline(Host.Pipeline);
    Resolve.setBindGroup(0, Host.Binding);
    Resolve.setBindGroup(1, MarchBinding(Host.Field));
    Resolve.draw(6);
    Resolve.end();

    Host.Device.queue.submit([Encoder.finish()]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 ENTRY PREVIEW
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 A thumbnail per card, showing what that entry alone produces.
//
//    🔴 The preview SPHERE-TRACES the analytic expression; it does not march the voxel grid. That is the
//       whole point of it: the grid holds one thing — the eroded rock — so grid-marching every card would
//       draw the same image on all of them. An upstream entry has no grid of its own, and never will,
//       because the sim seeds from the tree's root rather than from any intermediate.
//
//       This is why Resolve/SphereTrace.js survives. The M2 plan listed it for deletion as the viewport's
//       march, which it no longer is — but it is exactly the analytic march a preview needs.
//
//    📝 One offscreen target, reused for every card, then copied out. Cards share a size, so a per-card
//       texture would be N allocations of an identical thing.

const PreviewFieldOfView = 2.15;                                    // [-] range multiplier framing the subject

function ProvisionPreviewTarget(Host, Edge)
{
    if (Host.PreviewTarget && Host.PreviewEdge === Edge) return;

    if (Host.PreviewTarget) Host.PreviewTarget.destroy();

    Host.PreviewTarget = Host.Device.createTexture({
        label  : "EntryPreview",
        size   : [Edge, Edge],
        format : Host.Format,
        usage  : GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC
    });
    Host.PreviewEdge = Edge;

    // 📝 Bytes per row must be a multiple of 256 for a texture-to-buffer copy, so the readback buffer is
    //    padded and the extra is discarded when the rows are repacked.
    Host.PreviewStride = Math.ceil(Edge * 4 / 256) * 256;
    if (Host.PreviewReadback) Host.PreviewReadback.destroy();
    Host.PreviewReadback = Host.Device.createBuffer({
        size  : Host.PreviewStride * Edge,
        usage : GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
    });
}

// 📝 Compile a preview pipeline for one entry. Cached on the entry's own dial+topology stamp, because a
//    card that has not changed must not pay a shader compile every time the tree is rebuilt.
async function AcquirePreviewPipeline(Host, TreeState, Identifier, Stamp)
{
    Host.PreviewPipelines = Host.PreviewPipelines || new Map();

    const Held = Host.PreviewPipelines.get(Identifier);
    if (Held && Held.Stamp === Stamp) return Held.Pipeline;

    const Transcription = ConstructionTranscriber.TranscribeEntry(TreeState, Identifier);
    if (Transcription.Source === null) return null;

    const Source = DistanceExpressionPreamble + DialDeclaration
                 + Transcription.Source + SphereTraceResolve;

    Host.Device.pushErrorScope("validation");
    const Module = Host.Device.createShaderModule({ label: `Preview${Identifier}`, code: Source });

    const Pipeline = Host.Device.createRenderPipeline({
        layout : Host.Device.createPipelineLayout({ bindGroupLayouts: [Host.BindingLayout] }),
        vertex : { module: Module, entryPoint: "TranscribeQuadVertex" },
        fragment :
        {
            module     : Module,
            entryPoint : "InscribeSurfaceFragment",
            targets    : [{ format: Host.Format }]
        },
        primitive : { topology: "triangle-list" }
    });

    const Fault = await Host.Device.popErrorScope();
    if (Fault)
    {
        // 📝 Reported quietly. A broken preview must not present the same loud notice as a broken
        //    viewport — the tree still resolves, and one unrenderable card is not a failed edit.
        console.warn(`preview ${Identifier} failed to compile`, Fault.message);
        Host.PreviewPipelines.set(Identifier, { Stamp, Pipeline: null });
        return null;
    }

    Host.PreviewPipelines.set(Identifier, { Stamp, Pipeline });
    return Pipeline;
}

// 📝 Render one entry and paint the result into its card canvas.
//
//    🔴 Writes the SHARED uniform with a preview camera, so the caller MUST restore it before the next
//       viewport draw. RockFormationEntry does that by flagging a redraw, which rewrites the uniform from
//       ViewProfile — the uniform is never assumed to have survived a preview.
export async function InscribeEntryPreview(Host, TreeState, Identifier, Target, Stamp)
{
    if (!Host.Ready || !Host.BindingLayout) return false;

    const Pipeline = await AcquirePreviewPipeline(Host, TreeState, Identifier, Stamp);
    if (!Pipeline) return false;

    const Edge = Target.width;
    ProvisionPreviewTarget(Host, Edge);

    // ① A fixed three-quarter view. Every card shares it, so cards are comparable at a glance — a
    //    per-card camera would make two identical entries look different.
    const Range = DomainRadius * PreviewFieldOfView;
    const PreviewProfile =
    {
        Origin       : [Range * 0.62, Range * 0.44, Range * 0.72],
        Forward      : [-0.62, -0.44, -0.72],
        Rightward    : [0.757, 0.0, -0.653],
        Upward       : [-0.287, 0.898, -0.333],
        SolarBearing : [0.48, 0.72, 0.50],
        Viewport     : [Edge, Edge],

        StepScale    : 0.9,
        StepCeiling  : 96,
        HitTolerance : 0.0025,
        FarDistance  : Range * 2.4,
        SceneRadius  : DomainRadius * 1.8,

        AmbientWeight : 0.8,
        ShadowWeight  : 0.0,                                        // 📝 no second march at 96 px
        CavityWeight  : 0.35,
        SlopeWeight   : 0.25,
        Exposure      : 1.06,
        ResolveMode   : 0,
        PreviewMode   : 1                                           // matcap path — cheap and legible
    };

    WriteViewProfile(Host, TreeState, PreviewProfile);

    const Encoder = Host.Device.createCommandEncoder();
    const Resolve = Encoder.beginRenderPass({
        colorAttachments:
        [{
            view       : Host.PreviewTarget.createView(),
            clearValue : { r: 0.031, g: 0.031, b: 0.039, a: 1.0 },
            loadOp     : "clear",
            storeOp    : "store"
        }]
    });
    Resolve.setPipeline(Pipeline);
    Resolve.setBindGroup(0, Host.Binding);
    Resolve.draw(6);
    Resolve.end();

    Encoder.copyTextureToBuffer(
        { texture: Host.PreviewTarget },
        { buffer: Host.PreviewReadback, bytesPerRow: Host.PreviewStride, rowsPerImage: Edge },
        [Edge, Edge]);

    Host.Device.queue.submit([Encoder.finish()]);

    await Host.PreviewReadback.mapAsync(GPUMapMode.READ);
    const Raw = new Uint8Array(Host.PreviewReadback.getMappedRange()).slice();
    Host.PreviewReadback.unmap();

    // ② Repack the padded rows and swap BGRA to RGBA.
    //    🔴 The canvas format is bgra8unorm on every platform this runs on, but ImageData is RGBA. Copying
    //       straight across renders the rock blue and looks like a shading bug rather than a byte order one.
    const Pixels = new Uint8ClampedArray(Edge * Edge * 4);
    const Swap = Host.Format === "bgra8unorm";
    for (let Row = 0; Row < Edge; Row++)
    {
        const From = Row * Host.PreviewStride;
        const To   = Row * Edge * 4;
        for (let Column = 0; Column < Edge; Column++)
        {
            const A = From + Column * 4, B = To + Column * 4;
            Pixels[B    ] = Swap ? Raw[A + 2] : Raw[A];
            Pixels[B + 1] = Raw[A + 1];
            Pixels[B + 2] = Swap ? Raw[A] : Raw[A + 2];
            Pixels[B + 3] = 255;
        }
    }

    const Brush = Target.getContext("2d");
    if (Brush) Brush.putImageData(new ImageData(Pixels, Edge, Edge), 0, 0);
    return true;
}

export { UniformByteLength, ViewProfileVectors, DomainRadius };

// 📝 The CSS holds the canvas at its displayed size (aspect-ratio 1/1, width 100%), so shrinking the
//    backing store above never changes the layout — only how many pixels the march actually resolves.
