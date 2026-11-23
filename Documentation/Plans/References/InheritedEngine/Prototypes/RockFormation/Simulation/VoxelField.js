//========================================================================================================================
//                                                  VoxelField.js                                                  🧩
//========================================================================================================================
//
// 📝 The occupancy grid the erosion runs on: a ping-pong pair of f32 density buffers plus one static
//    resistance buffer, and the bind groups that expose them to the compute and march shaders.
//
//    Density is FRACTIONAL OCCUPANCY in [0,1], not a signed distance. A cell holding 0.4 is 40% rock.
//    That is what makes erosion expressible at all — you cannot remove "a little" from a boolean cell,
//    and an SDF cannot be locally decremented without ceasing to be a distance field.
//
//    🔴 WGSL HAS NO f32 ATOMICS. Every process must therefore be GATHER, never SCATTER: a cell reads its
//       neighbours from the READ buffer and writes only its own index in the WRITE buffer. Writing to a
//       neighbour's index is a data race that no validation layer will report — it produces a plausible
//       image with quietly non-conserved mass, which is why CheckMassConservation exists.
//
//    ⚠️ A density grid is NOT a distance field. Consumers must fixed-step march it; sphere-tracing step
//       lengths read off this grid are meaningless and will tunnel straight through a thin ligament.

//------------------------------------------------------------------------------------------------------------------------
//                                                   GRID SIZES
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Two tiers, the same two-speed logic as the render: weather at 128³ while authoring, bake at 256³.
//
//    Arithmetic, because "will it fit" is not a matter of opinion:
//
//      128³ = 2 097 152 cells x 4 B =   8 MiB   -> density pair 16 MiB + resistance  8 MiB =  24 MiB
//      256³ = 16 777 216 cells x 4 B =  64 MiB  -> density pair 128 MiB + resistance 64 MiB = 192 MiB
//
//    🔴 WebGPU's DEFAULT maxBufferSize is 256 MiB and maxStorageBufferBindingSize is 128 MiB. A single
//       256³ f32 buffer is 64 MiB, so each binding is legal — but the 192 MiB total is most of the default
//       device allocation. 512³ would be 512 MiB per buffer and busts maxBufferSize outright; it is not a
//       tier that can be reached by turning a dial.

export const PreviewEdge = 128;                                     // [idx] - cells per axis while authoring
export const BakeEdge    = 256;                                     // [idx] - cells per axis for the bake

// 📝 One workgroup covers a 4x4x4 block. 64 invocations is a full wavefront on AMD and two warps on
//    NVIDIA, and a cubic block keeps the 26 neighbour reads of a gather inside one block's cache
//    footprint far more often than a 64x1x1 line would.
export const WorkgroupEdge = 4;                                     // [idx] - cells per workgroup axis

//------------------------------------------------------------------------------------------------------------------------
//                                                  FIELD PROFILE
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The uniform head the compute shaders read. Same lane discipline as the render's DialProfile:
//    COUNT THE LANES, NOT THE SCALARS. This head is deliberately all-scalar and all-vec4-aligned so the
//    question cannot arise — 3 lanes, 12 scalars, no vec3f anywhere.
//
//      0  Edge, InverseEdge, CellSize, DomainRadius
//      1  StepIndex, StepDelta, WindBearing, WindStrength
//      2  GravityDrop, ReposeSlope, SeedTally, Reserved
//
//    ⚠️ Adding a field means re-counting lanes HERE, in ComposeFieldDeclaration, and in WriteFieldProfile.
//       Three places, one layout — the 848-vs-880 bug came from exactly this kind of split.
export const FieldProfileVectors = 3;                               // [idx] - vec4 lanes in the compute head
export const FieldProfileBytes   = FieldProfileVectors * 16;        // [-]

// 📝 The WGSL declaration, exported so the compute shaders and the grid march share ONE source of truth
//    rather than each restating the layout.
export const FieldProfileDeclaration = `
struct FieldProfile
{
    Edge          : f32,
    InverseEdge   : f32,
    CellSize      : f32,
    DomainRadius  : f32,
    StepIndex     : f32,
    StepDelta     : f32,
    WindBearing   : f32,
    WindStrength  : f32,
    GravityDrop   : f32,
    ReposeSlope   : f32,
    SeedTally     : f32,
    Reserved      : f32
};
`;

// 📝 Index helpers, shared by every consumer. Written once here because an x/z transposition between the
//    seed shader and the march shader renders a mirrored rock that looks entirely plausible.
export const FieldAddressing = `
fn CellToOrdinal(Cell : vec3i, Edge : i32) -> i32
{
    return (Cell.z * Edge + Cell.y) * Edge + Cell.x;
}

fn OrdinalToCell(Ordinal : i32, Edge : i32) -> vec3i
{
    let X = Ordinal % Edge;
    let Y = (Ordinal / Edge) % Edge;
    let Z = Ordinal / (Edge * Edge);
    return vec3i(X, Y, Z);
}

// 📝 Cell centre -> world position, centred on the origin. The +0.5 puts the sample at the cell CENTRE;
//    ⚠️ omitting it shifts the whole field by half a cell against the analytic seed, which reads as the
//    rock sitting slightly off its own bounding box.
fn CellToPosition(Cell : vec3i, Edge : f32, CellSize : f32) -> vec3f
{
    let Middle = (Edge - 1.0) * 0.5;
    return (vec3f(Cell) - vec3f(Middle)) * CellSize;
}

// 🔴 Out-of-domain reads return 0.0 (empty), NOT a clamped edge sample. Clamping would make the boundary
//    cells appear to have infinite material beside them, so thermal collapse would pull mass in from
//    nowhere and the domain walls would grow buttresses.
fn ReadDensity(Cell : vec3i, Edge : i32) -> f32
{
    if (any(Cell < vec3i(0)) || any(Cell >= vec3i(Edge))) { return 0.0; }
    return DensityRead[CellToOrdinal(Cell, Edge)];
}

fn ReadResistance(Cell : vec3i, Edge : i32) -> f32
{
    if (any(Cell < vec3i(0)) || any(Cell >= vec3i(Edge))) { return 1.0; }
    return Resistance[CellToOrdinal(Cell, Edge)];
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                  FIELD STORAGE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Allocate a field at the given edge. Called once per tier; switching tiers destroys and reallocates
//    rather than resizing, because a WebGPU buffer has no resize.
export function ComposeVoxelField(Device, Edge, DomainRadius)
{
    const CellTally = Edge * Edge * Edge;
    const ByteSpan  = CellTally * 4;                                // [-] f32 per cell

    // 🔴 COPY_SRC on both density buffers, not just one. Read-back for the mass check must be able to
    //    reach whichever buffer the ping-pong last wrote to, and which one that is depends on the parity
    //    of the step count.
    const DensityUsage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST;

    const Field =
    {
        Edge,
        CellTally,
        ByteSpan,
        DomainRadius,                                               // [m] - half-extent of the grid in world units
        CellSize     : (DomainRadius * 2.0) / Edge,                 // [m]

        // ① The ping-pong pair. Parity tracks which one currently holds the live density.
        DensityPair  :
        [
            Device.createBuffer({ size: ByteSpan, usage: DensityUsage, label: "DensityA" }),
            Device.createBuffer({ size: ByteSpan, usage: DensityUsage, label: "DensityB" })
        ],
        Parity       : 0,                                           // [idx] - DensityPair[Parity] is the READ buffer

        // ② Resistance is written once by the seed and read for the whole run. No ping-pong: nothing
        //    erodes the rock's hardness, only its presence.
        Resistance   : Device.createBuffer({
            size  : ByteSpan,
            usage : GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
            label : "Resistance"
        }),

        Profile      : Device.createBuffer({
            size  : FieldProfileBytes,
            usage : GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
            label : "FieldProfile"
        }),

        Scratch      : new Float32Array(FieldProfileVectors * 4),

        StepTally    : 0,                                           // [idx] - erosion steps run since the last reseed
        Readback     : null                                         // lazily allocated by ReadDensityField
    };

    Field.Layouts  = ComposeFieldLayouts(Device);
    Field.Bindings = ComposeFieldBindings(Device, Field);
    return Field;
}

export function DiscardVoxelField(Field)
{
    if (!Field) return;
    Field.DensityPair[0].destroy();
    Field.DensityPair[1].destroy();
    Field.Resistance.destroy();
    Field.Profile.destroy();
    if (Field.Readback) Field.Readback.destroy();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  BIND LAYOUTS
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Two layouts, because the two consumers have genuinely different access:
//
//      ErosionLayout   compute   0 profile  1 density READ (ro)  2 density WRITE (rw)  3 resistance (ro)
//      MarchLayout     fragment  0 profile  1 density READ (ro)                        2 resistance (ro)
//
//    🔴 The read binding is "read-only-storage" and the write binding is "storage". Declaring the read
//       side writable would compile and run, and would let a mistyped index in a gather silently corrupt
//       the buffer being iterated — the exact failure the ping-pong exists to make impossible.

function ComposeFieldLayouts(Device)
{
    const ErosionLayout = Device.createBindGroupLayout({
        label: "ErosionLayout",
        entries:
        [
            { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } },
            { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } }
        ]
    });

    const MarchLayout = Device.createBindGroupLayout({
        label: "MarchLayout",
        entries:
        [
            { binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
            { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
            { binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } }
        ]
    });

    return { ErosionLayout, MarchLayout };
}

// 📝 Both ping-pong orientations are built ONCE at allocation. Creating a bind group per dispatch would
//    put object allocation inside the step loop, and the pair only has two possible arrangements.
function ComposeFieldBindings(Device, Field)
{
    const { ErosionLayout, MarchLayout } = Field.Layouts;
    const [A, B] = Field.DensityPair;

    const Erode = [[A, B], [B, A]].map(([Read, Write], Index) => Device.createBindGroup({
        layout  : ErosionLayout,
        label   : `Erode${Index}`,
        entries :
        [
            { binding: 0, resource: { buffer: Field.Profile } },
            { binding: 1, resource: { buffer: Read } },
            { binding: 2, resource: { buffer: Write } },
            { binding: 3, resource: { buffer: Field.Resistance } }
        ]
    }));

    const March = [A, B].map((Read, Index) => Device.createBindGroup({
        layout  : MarchLayout,
        label   : `March${Index}`,
        entries :
        [
            { binding: 0, resource: { buffer: Field.Profile } },
            { binding: 1, resource: { buffer: Read } },
            { binding: 2, resource: { buffer: Field.Resistance } }
        ]
    }));

    return { Erode, March };
}

// 📝 The bind group whose READ side is the live density. Both consumers must agree on parity or the march
//    displays the buffer the next step is about to overwrite — which shows as a one-step-stale flicker.
export function ErosionBinding(Field) { return Field.Bindings.Erode[Field.Parity]; }
export function MarchBinding(Field)   { return Field.Bindings.March[Field.Parity]; }

// 🔴 Called ONCE per dispatch, AFTER the dispatch is encoded. Flipping before means the shader wrote into
//    the buffer the next read will treat as stale, and the sim appears to make no progress at all.
export function FlipParity(Field)
{
    Field.Parity = Field.Parity ^ 1;
    Field.StepTally++;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PROFILE WRITE
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ Lane order MUST match FieldProfileDeclaration above. Three Put calls, three lanes.
export function WriteFieldProfile(Device, Field, Weather)
{
    const S = Field.Scratch;
    let Lane = 0;
    const Put = (x, y, z, w) => { S[Lane++] = x; S[Lane++] = y; S[Lane++] = z; S[Lane++] = w; };

    Put(Field.Edge, 1.0 / Field.Edge, Field.CellSize, Field.DomainRadius);
    Put(Field.StepTally, Weather.StepDelta, Weather.WindBearing, Weather.WindStrength);
    Put(Weather.GravityDrop, Weather.ReposeSlope, Weather.SeedTally, 0.0);

    Device.queue.writeBuffer(Field.Profile, 0, S.buffer, 0, FieldProfileBytes);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DISPATCH
//------------------------------------------------------------------------------------------------------------------------

// 📝 Workgroup count per axis. Edge is a power of two and WorkgroupEdge is 4, so this divides exactly —
//    but ceil() is kept so a non-multiple edge degrades into a partial block the shader bounds-checks,
//    rather than silently leaving a slab of the grid never dispatched.
export function DispatchSpan(Field)
{
    const Blocks = Math.ceil(Field.Edge / WorkgroupEdge);
    return [Blocks, Blocks, Blocks];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    READ BACK
//------------------------------------------------------------------------------------------------------------------------

// 📝 Copy the live density to the CPU. This is for VERIFICATION — mass conservation, connectivity — not
//    for the render, which reads the buffer on the GPU where it already lives.
//
//    🔴 Read-back STALLS the pipeline: it waits for every queued dispatch to retire. Calling it per frame
//       would serialise CPU and GPU and cost far more than the erosion itself. It exists for the harness.
export async function ReadDensityField(Device, Field)
{
    if (!Field.Readback)
    {
        Field.Readback = Device.createBuffer({
            size  : Field.ByteSpan,
            usage : GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
            label : "DensityReadback"
        });
    }

    const Encoder = Device.createCommandEncoder();
    Encoder.copyBufferToBuffer(Field.DensityPair[Field.Parity], 0, Field.Readback, 0, Field.ByteSpan);
    Device.queue.submit([Encoder.finish()]);

    await Field.Readback.mapAsync(GPUMapMode.READ);
    const Copy = new Float32Array(Field.Readback.getMappedRange().slice(0));
    Field.Readback.unmap();
    return Copy;
}

// 📝 Total occupancy. Thermal collapse must leave this UNCHANGED (mass moves, it is not created); the
//    abrading processes must only ever reduce it. A rise is a gather written as a scatter.
export function TotalMass(Density)
{
    let Sum = 0.0;
    for (let Index = 0; Index < Density.length; Index++) Sum += Density[Index];
    return Sum;
}
